#ifndef WRESTLEFEST_ASSET_LOADER_H
#define WRESTLEFEST_ASSET_LOADER_H

#include "obb_archive.h"

#include <cstdint>
#include <string>
#include <vector>

struct AAssetManager;
struct android_app;

class AssetLoader
{
public:
    AssetLoader();
    ~AssetLoader();

    AssetLoader(const AssetLoader &) = delete;
    AssetLoader &operator=(const AssetLoader &) = delete;

    bool initialize(android_app *app);
    bool initialize(AAssetManager *assets, const std::string &explicit_obb_path = "");

    bool readAsset(const std::string &path, std::vector<std::uint8_t> *bytes);
    bool hasAsset(const std::string &path) const;

    bool isObbMounted() const;
    const std::string &getObbPath() const;
    AAssetManager *getAssetManager() const;

private:
    std::string findObbViaJni(android_app *app);
    std::string findObbInFileSystem();

    AAssetManager *assets_ = nullptr;
    ObbArchive obb_;
};

#endif // WRESTLEFEST_ASSET_LOADER_H
