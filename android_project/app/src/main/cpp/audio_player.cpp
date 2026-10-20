#include "audio_player.h"
#include "asset_loader.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <cstring>

namespace {

constexpr char kLogTag[] = "WrestleFestAudio";
constexpr std::size_t kMaxWavBytes = 32U * 1024U * 1024U;

struct WavInfo {
    std::size_t data_offset;
    std::size_t data_size;
    std::uint32_t sample_rate;
    std::uint16_t channels;
    std::uint16_t bits_per_sample;
};

std::uint16_t readU16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t readU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool parsePcmWav(const std::vector<std::uint8_t>& bytes, WavInfo* info) {
    if (info == nullptr || bytes.size() < 12 ||
        std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool has_format = false;
    bool has_data = false;
    std::uint16_t channels = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint16_t audio_format = 0;
    std::uint32_t sample_rate = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
        const std::uint8_t* chunk = bytes.data() + offset;
        const std::uint32_t chunk_size = readU32(chunk + 4);
        const std::size_t payload = offset + 8;
        if (chunk_size > bytes.size() - payload) {
            return false;
        }

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return false;
            }
            audio_format = readU16(bytes.data() + payload);
            channels = readU16(bytes.data() + payload + 2);
            sample_rate = readU32(bytes.data() + payload + 4);
            bits_per_sample = readU16(bytes.data() + payload + 14);
            has_format = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data_offset = payload;
            data_size = chunk_size;
            has_data = true;
        }

        const std::size_t padded_size = static_cast<std::size_t>(chunk_size) + (chunk_size & 1U);
        if (padded_size > bytes.size() - payload) {
            break;
        }
        offset = payload + padded_size;
    }

    if (!has_format || !has_data || audio_format != 1 ||
        (channels != 1 && channels != 2) ||
        (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24) ||
        sample_rate < 8000 || sample_rate > 192000 ||
        data_size == 0 || data_size > kMaxWavBytes ||
        data_size % (channels * (bits_per_sample / 8U)) != 0) {
        return false;
    }

    *info = {data_offset, data_size, sample_rate, channels, bits_per_sample};
    return true;
}

bool readAsset(AAssetManager* assets, const std::string& path,
               std::vector<std::uint8_t>* bytes) {
    if (assets == nullptr || bytes == nullptr) {
        return false;
    }

    AAsset* asset = AAssetManager_open(assets, path.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Missing audio asset: %s", path.c_str());
        return false;
    }

    const off64_t length = AAsset_getLength64(asset);
    if (length <= 0 || static_cast<std::uint64_t>(length) > kMaxWavBytes + 1024U * 1024U) {
        AAsset_close(asset);
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Audio asset has invalid size: %s", path.c_str());
        return false;
    }

    bytes->resize(static_cast<std::size_t>(length));
    std::size_t consumed = 0;
    while (consumed < bytes->size()) {
        const int count = AAsset_read(asset, bytes->data() + consumed, bytes->size() - consumed);
        if (count <= 0) {
            AAsset_close(asset);
            return false;
        }
        consumed += static_cast<std::size_t>(count);
    }
    AAsset_close(asset);
    return true;
}

}  // namespace

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize() {
    if (engine_ != nullptr) {
        return true;
    }

    if (slCreateEngine(&engine_object_, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS ||
        (*engine_object_)->Realize(engine_object_, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
        (*engine_object_)->GetInterface(engine_object_, SL_IID_ENGINE, &engine_) != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenSL ES engine initialization failed");
        shutdown();
        return false;
    }

    if ((*engine_)->CreateOutputMix(engine_, &output_mix_object_, 0, nullptr, nullptr) !=
            SL_RESULT_SUCCESS ||
        (*output_mix_object_)->Realize(output_mix_object_, SL_BOOLEAN_FALSE) !=
            SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenSL ES output mix initialization failed");
        shutdown();
        return false;
    }
    return true;
}

bool AudioPlayer::prepareWav(AssetLoader* assets, const std::string& asset_path) {
    if (!initialize() || assets == nullptr) {
        return false;
    }

    std::vector<std::uint8_t> wav;
    if (!assets->readAsset(asset_path, &wav)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Failed to load audio asset: %s",
                            asset_path.c_str());
        return false;
    }
    return prepareWavBytes(wav);
}

bool AudioPlayer::prepareWav(AAssetManager* assets, const std::string& asset_path) {
    if (!initialize()) {
        return false;
    }

    std::vector<std::uint8_t> wav;
    if (!readAsset(assets, asset_path, &wav)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Unsupported PCM WAV asset: %s",
                            asset_path.c_str());
        return false;
    }
    return prepareWavBytes(wav);
}

bool AudioPlayer::prepareWavBytes(const std::vector<std::uint8_t>& wav) {
    if (!initialize()) {
        return false;
    }

    WavInfo info{};
    if (!parsePcmWav(wav, &info)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Unsupported PCM WAV format");
        return false;
    }

    destroyPlayer();
    if (info.bits_per_sample == 8) {
        const std::size_t sample_count = info.data_size;
        pcm_.resize(sample_count * sizeof(std::int16_t));
        for (std::size_t i = 0; i < sample_count; ++i) {
            const std::int16_t sample = static_cast<std::int16_t>(
                (static_cast<int>(wav[info.data_offset + i]) - 128) * 256);
            std::memcpy(pcm_.data() + i * sizeof(sample), &sample, sizeof(sample));
        }
    } else if (info.bits_per_sample == 24) {
        const std::size_t sample_count = info.data_size / 3U;
        pcm_.resize(sample_count * sizeof(std::int16_t));
        for (std::size_t i = 0; i < sample_count; ++i) {
            const std::size_t source = info.data_offset + i * 3U;
            std::int32_t sample = static_cast<std::int32_t>(wav[source]) |
                                  (static_cast<std::int32_t>(wav[source + 1]) << 8U) |
                                  (static_cast<std::int32_t>(wav[source + 2]) << 16U);
            if ((sample & 0x00800000) != 0) {
                sample -= 0x01000000;
            }
            const std::int16_t converted = static_cast<std::int16_t>(sample / 256);
            std::memcpy(pcm_.data() + i * sizeof(converted), &converted, sizeof(converted));
        }
    } else {
        pcm_.assign(wav.begin() + static_cast<std::ptrdiff_t>(info.data_offset),
                    wav.begin() + static_cast<std::ptrdiff_t>(info.data_offset + info.data_size));
    }

    SLDataLocator_AndroidSimpleBufferQueue queue_locator = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1};
    SLDataFormat_PCM format = {
        SL_DATAFORMAT_PCM,
        static_cast<SLuint32>(info.channels),
        static_cast<SLuint32>(info.sample_rate * 1000U),
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        info.channels == 1
            ? SL_SPEAKER_FRONT_CENTER
            : static_cast<SLuint32>(SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT),
        SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource source = {&queue_locator, &format};
    SLDataLocator_OutputMix output_locator = {
        SL_DATALOCATOR_OUTPUTMIX, output_mix_object_};
    SLDataSink sink = {&output_locator, nullptr};
    const SLInterfaceID interface_ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean interface_required[] = {SL_BOOLEAN_TRUE};

    if ((*engine_)->CreateAudioPlayer(engine_, &player_object_, &source, &sink, 1,
                                      interface_ids, interface_required) != SL_RESULT_SUCCESS ||
        (*player_object_)->Realize(player_object_, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
        (*player_object_)->GetInterface(player_object_, SL_IID_PLAY, &player_) != SL_RESULT_SUCCESS ||
        (*player_object_)->GetInterface(player_object_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                        &queue_) != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenSL ES player setup failed");
        destroyPlayer();
        pcm_.clear();
        return false;
    }
    return true;
}

bool AudioPlayer::play() {
    if (player_ == nullptr || queue_ == nullptr || pcm_.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Audio playback requested before WAV preparation");
        return false;
    }
    if ((*player_)->SetPlayState(player_, SL_PLAYSTATE_STOPPED) != SL_RESULT_SUCCESS ||
        (*queue_)->Clear(queue_) != SL_RESULT_SUCCESS ||
        (*queue_)->Enqueue(queue_, pcm_.data(), static_cast<SLuint32>(pcm_.size())) !=
            SL_RESULT_SUCCESS ||
        (*player_)->SetPlayState(player_, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenSL ES buffer queue playback failed");
        return false;
    }
    return true;
}

void AudioPlayer::destroyPlayer() noexcept {
    if (player_ != nullptr) {
        (*player_)->SetPlayState(player_, SL_PLAYSTATE_STOPPED);
        player_ = nullptr;
    }
    if (queue_ != nullptr) {
        (*queue_)->Clear(queue_);
        queue_ = nullptr;
    }
    if (player_object_ != nullptr) {
        (*player_object_)->Destroy(player_object_);
        player_object_ = nullptr;
    }
}

void AudioPlayer::shutdown() noexcept {
    destroyPlayer();
    pcm_.clear();
    for (auto& entry : cues_) {
        destroyCue(entry.second.get());
    }
    cues_.clear();
    if (output_mix_object_ != nullptr) {
        (*output_mix_object_)->Destroy(output_mix_object_);
        output_mix_object_ = nullptr;
    }
    engine_ = nullptr;
    if (engine_object_ != nullptr) {
        (*engine_object_)->Destroy(engine_object_);
        engine_object_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Short cues
// ---------------------------------------------------------------------------
bool AudioPlayer::decodeWav(const std::vector<std::uint8_t>& wav,
                            std::vector<std::uint8_t>* pcm) {
    WavInfo info{};
    if (pcm == nullptr || !parsePcmWav(wav, &info)) return false;
    pcm->clear();
    if (info.bits_per_sample == 8) {
        pcm->resize(info.data_size * sizeof(std::int16_t));
        for (std::size_t i = 0; i < info.data_size; ++i) {
            const std::int16_t sample = static_cast<std::int16_t>(
                (static_cast<int>(wav[info.data_offset + i]) - 128) * 256);
            std::memcpy(pcm->data() + i * sizeof(sample), &sample, sizeof(sample));
        }
    } else if (info.bits_per_sample == 24) {
        const std::size_t sample_count = info.data_size / 3U;
        pcm->resize(sample_count * sizeof(std::int16_t));
        for (std::size_t i = 0; i < sample_count; ++i) {
            const std::size_t source = info.data_offset + i * 3U;
            std::int32_t sample = static_cast<std::int32_t>(wav[source]) |
                                  (static_cast<std::int32_t>(wav[source + 1]) << 8U) |
                                  (static_cast<std::int32_t>(wav[source + 2]) << 16U);
            if ((sample & 0x00800000) != 0) sample -= 0x01000000;
            const std::int16_t converted = static_cast<std::int16_t>(sample / 256);
            std::memcpy(pcm->data() + i * sizeof(converted), &converted, sizeof(converted));
        }
    } else {
        pcm->assign(wav.begin() + static_cast<std::ptrdiff_t>(info.data_offset),
                    wav.begin() + static_cast<std::ptrdiff_t>(info.data_offset + info.data_size));
    }
    return !pcm->empty();
}

bool AudioPlayer::createCue(const std::vector<std::uint8_t>& wav, Cue* cue) {
    if (cue == nullptr || !initialize()) return false;
    WavInfo info{};
    if (!parsePcmWav(wav, &info)) return false;
    if (!decodeWav(wav, &cue->pcm)) return false;

    SLDataLocator_AndroidSimpleBufferQueue queue_locator = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1};
    SLDataFormat_PCM format = {
        SL_DATAFORMAT_PCM,
        static_cast<SLuint32>(info.channels),
        static_cast<SLuint32>(info.sample_rate * 1000U),
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        info.channels == 1
            ? SL_SPEAKER_FRONT_CENTER
            : static_cast<SLuint32>(SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT),
        SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource source = {&queue_locator, &format};
    SLDataLocator_OutputMix output_locator = {SL_DATALOCATOR_OUTPUTMIX, output_mix_object_};
    SLDataSink sink = {&output_locator, nullptr};
    const SLInterfaceID interface_ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean interface_required[] = {SL_BOOLEAN_TRUE};

    if ((*engine_)->CreateAudioPlayer(engine_, &cue->player_object, &source, &sink, 1,
                                      interface_ids, interface_required) != SL_RESULT_SUCCESS ||
        (*cue->player_object)->Realize(cue->player_object, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
        (*cue->player_object)->GetInterface(cue->player_object, SL_IID_PLAY, &cue->play) !=
            SL_RESULT_SUCCESS ||
        (*cue->player_object)->GetInterface(cue->player_object, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                            &cue->queue) != SL_RESULT_SUCCESS) {
        destroyCue(cue);
        return false;
    }
    cue->ready = true;
    return true;
}

void AudioPlayer::destroyCue(Cue* cue) noexcept {
    if (cue == nullptr) return;
    cue->ready = false;
    if (cue->player_object != nullptr) {
        (*cue->player_object)->Destroy(cue->player_object);
        cue->player_object = nullptr;
    }
    cue->play = nullptr;
    cue->queue = nullptr;
    cue->pcm.clear();
}

AudioPlayer::Cue* AudioPlayer::cueFor(const std::string& asset_path) {
    auto found = cues_.find(asset_path);
    if (found != cues_.end()) return found->second.get();

    // One resident player per path: the game's cue set is small (bell, hits,
    // pain, effort) and matches never overlap more than a handful of them.
    std::unique_ptr<Cue> cue(new Cue());
    cues_.emplace(asset_path, std::move(cue));
    return cues_.find(asset_path)->second.get();
}

bool AudioPlayer::playAsset(AssetLoader* assets, const std::string& asset_path) {
    if (assets == nullptr || !initialize()) return false;
    Cue* cue = cueFor(asset_path);
    if (cue == nullptr) return false;
    if (!cue->ready) {
        std::vector<std::uint8_t> wav;
        if (!assets->readAsset(asset_path, &wav)) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "missing cue %s", asset_path.c_str());
            return false;
        }
        // A failed cue is remembered as empty so a missing file is logged once.
        if (!createCue(wav, cue)) {
            cue->ready = false;
            cue->pcm.clear();
            return false;
        }
    }
    if (cue->play == nullptr || cue->queue == nullptr || cue->pcm.empty()) return false;
    if ((*cue->play)->SetPlayState(cue->play, SL_PLAYSTATE_STOPPED) != SL_RESULT_SUCCESS ||
        (*cue->queue)->Clear(cue->queue) != SL_RESULT_SUCCESS ||
        (*cue->queue)->Enqueue(cue->queue, cue->pcm.data(),
                               static_cast<SLuint32>(cue->pcm.size())) != SL_RESULT_SUCCESS ||
        (*cue->play)->SetPlayState(cue->play, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS) {
        return false;
    }
    return true;
}
