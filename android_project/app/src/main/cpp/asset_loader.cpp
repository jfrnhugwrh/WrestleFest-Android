#include "asset_loader.h"

#include <android/asset_manager.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

namespace
{

    constexpr char kLogTag[] = "WrestleFestNative";
    constexpr char kPackageName[] = "com.mfm.wrestlefest";
    constexpr std::size_t kMaxAssetBytes = 64U * 1024U * 1024U;

    bool fileExists(const std::string &path)
    {
        return ::access(path.c_str(), R_OK) == 0;
    }

} // namespace

AssetLoader::AssetLoader() = default;

AssetLoader::~AssetLoader()
{
    obb_.close();
    assets_ = nullptr;
}

bool AssetLoader::isObbMounted() const
{
    return obb_.isOpen();
}

const std::string &AssetLoader::getObbPath() const
{
    return obb_.getPath();
}

AAssetManager *AssetLoader::getAssetManager() const
{
    return assets_;
}

std::string AssetLoader::findObbViaJni(android_app *app)
{
    if (app == nullptr || app->activity == nullptr || app->activity->vm == nullptr)
    {
        return "";
    }

    JavaVM *vm = app->activity->vm;
    jobject activity = app->activity->javaGameActivity;
    JNIEnv *env = nullptr;

    const jint attach_result = vm->AttachCurrentThread(&env, nullptr);
    if (attach_result != JNI_OK || env == nullptr)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Could not attach JNI thread for OBB lookup");
        return "";
    }

    std::string found_path;
    jclass clazz = env->GetObjectClass(activity);
    if (clazz != nullptr)
    {
        jmethodID method = env->GetMethodID(clazz, "findObbFile", "()Ljava/lang/String;");
        if (method != nullptr)
        {
            auto j_str = static_cast<jstring>(env->CallObjectMethod(activity, method));
            if (j_str != nullptr)
            {
                const char *chars = env->GetStringUTFChars(j_str, nullptr);
                if (chars != nullptr && chars[0] != '\0')
                {
                    found_path = chars;
                }
                env->ReleaseStringUTFChars(j_str, chars);
                env->DeleteLocalRef(j_str);
            }
        }
        env->DeleteLocalRef(clazz);
    }

    return found_path;
}

std::string AssetLoader::findObbInFileSystem()
{
    const std::vector<std::string> candidate_dirs = {
        std::string("/sdcard/Android/obb/") + kPackageName,
        std::string("/sdcard/obb/") + kPackageName,
        std::string("/storage/emulated/0/Android/obb/") + kPackageName,
        std::string("/storage/emulated/0/obb/") + kPackageName,
        std::string("/data/data/") + kPackageName + "/files/obb/" + kPackageName,
        std::string("/data/user/0/") + kPackageName + "/files/obb/" + kPackageName,
    };

    const std::vector<std::string> candidate_filenames = {
        std::string("main.1.") + kPackageName + ".obb",
        std::string("main.") + kPackageName + ".obb",
    };

    for (const auto &dir_path : candidate_dirs)
    {
        // Direct probe for expected standard filename
        for (const auto &fn : candidate_filenames)
        {
            const std::string probe = dir_path + "/" + fn;
            if (fileExists(probe))
            {
                return probe;
            }
        }

        // Directory listing search for any *.obb
        DIR *dir = ::opendir(dir_path.c_str());
        if (dir != nullptr)
        {
            struct dirent *entry = nullptr;
            while ((entry = ::readdir(dir)) != nullptr)
            {
                const std::string name(entry->d_name);
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".obb") == 0)
                {
                    const std::string full = dir_path + "/" + name;
                    if (fileExists(full))
                    {
                        ::closedir(dir);
                        return full;
                    }
                }
            }
            ::closedir(dir);
        }
    }

    return "";
}

bool AssetLoader::initialize(android_app *app)
{
    if (app != nullptr && app->activity != nullptr)
    {
        assets_ = app->activity->assetManager;
    }

    std::string obb_path = findObbViaJni(app);
    if (obb_path.empty())
    {
        obb_path = findObbInFileSystem();
    }

    if (!obb_path.empty())
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Found candidate OBB: %s", obb_path.c_str());
        if (obb_.open(obb_path))
        {
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "Successfully mounted OBB expansion file (%zu entries): %s",
                                obb_.entryCount(), obb_path.c_str());
            return true;
        }
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "Failed to mount candidate OBB at '%s', falling back to APK assets",
                            obb_path.c_str());
    }
    else
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "No OBB expansion file detected in standard search paths. Using APK assets.");
    }

    return assets_ != nullptr;
}

bool AssetLoader::initialize(AAssetManager *assets, const std::string &explicit_obb_path)
{
    assets_ = assets;
    if (!explicit_obb_path.empty())
    {
        if (obb_.open(explicit_obb_path))
        {
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "Mounted explicit OBB (%zu entries): %s", obb_.entryCount(),
                                explicit_obb_path.c_str());
            return true;
        }
    }
    return assets_ != nullptr;
}

bool AssetLoader::hasAsset(const std::string &path) const
{
    if (obb_.isOpen() && obb_.hasEntry(path))
    {
        return true;
    }
    if (assets_ != nullptr)
    {
        AAsset *asset = AAssetManager_open(assets_, path.c_str(), AASSET_MODE_UNKNOWN);
        if (asset != nullptr)
        {
            AAsset_close(asset);
            return true;
        }
    }
    return false;
}

bool AssetLoader::readAsset(const std::string &path, std::vector<std::uint8_t> *bytes)
{
    if (bytes == nullptr)
    {
        return false;
    }

    // 1. Try OBB expansion file
    if (obb_.isOpen())
    {
        if (obb_.readFile(path, bytes))
        {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded asset from OBB: %s (%zu bytes)",
                                path.c_str(), bytes->size());
            return true;
        }
    }

    // 2. Fall back to APK assets
    if (assets_ != nullptr)
    {
        AAsset *asset = AAssetManager_open(assets_, path.c_str(), AASSET_MODE_BUFFER);
        if (asset != nullptr)
        {
            const off64_t length = AAsset_getLength64(asset);
            if (length > 0 && static_cast<std::uint64_t>(length) <= kMaxAssetBytes)
            {
                bytes->resize(static_cast<std::size_t>(length));
                std::size_t consumed = 0;
                bool read_ok = true;
                while (consumed < bytes->size())
                {
                    const int count =
                        AAsset_read(asset, bytes->data() + consumed, bytes->size() - consumed);
                    if (count <= 0)
                    {
                        read_ok = false;
                        break;
                    }
                    consumed += static_cast<std::size_t>(count);
                }
                AAsset_close(asset);
                if (read_ok)
                {
                    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                        "Loaded asset from APK: %s (%zu bytes)", path.c_str(),
                                        bytes->size());
                    return true;
                }
            }
            else
            {
                AAsset_close(asset);
            }
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "Asset not found in OBB [%s] or APK assets: %s",
                        obb_.isOpen() ? obb_.getPath().c_str() : "none", path.c_str());
    return false;
}
