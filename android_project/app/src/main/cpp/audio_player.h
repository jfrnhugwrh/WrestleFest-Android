#ifndef WRESTLEFEST_AUDIO_PLAYER_H
#define WRESTLEFEST_AUDIO_PLAYER_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <cstdint>
#include <string>
#include <vector>

struct AAssetManager;
class AssetLoader;

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool initialize();
    bool prepareWav(AssetLoader* assets, const std::string& asset_path);
    bool prepareWav(AAssetManager* assets, const std::string& asset_path);
    bool prepareWavBytes(const std::vector<std::uint8_t>& wav);
    bool play();
    void shutdown() noexcept;

private:
    void destroyPlayer() noexcept;

    SLObjectItf engine_object_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLObjectItf output_mix_object_ = nullptr;
    SLObjectItf player_object_ = nullptr;
    SLPlayItf player_ = nullptr;
    SLAndroidSimpleBufferQueueItf queue_ = nullptr;
    std::vector<std::uint8_t> pcm_;
};

#endif
