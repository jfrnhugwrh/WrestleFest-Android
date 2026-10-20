#ifndef WRESTLEFEST_AUDIO_PLAYER_H
#define WRESTLEFEST_AUDIO_PLAYER_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
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
    // Play an asset with an independent player, so the short game cues can
    // overlap. Slots are pooled by the caller (see game/app.cpp).
    bool playAsset(AssetLoader* assets, const std::string& asset_path);
    void shutdown() noexcept;

private:
    void destroyPlayer() noexcept;

    // Short cues are kept resident per slot so they can be retriggered without
    // re-reading and re-decoding the WAV every time.
    struct Cue {
        SLObjectItf player_object = nullptr;
        SLPlayItf play = nullptr;
        SLAndroidSimpleBufferQueueItf queue = nullptr;
        std::vector<std::uint8_t> pcm;
        bool ready = false;
    };
    bool createCue(const std::vector<std::uint8_t>& pcm, Cue* cue);
    void destroyCue(Cue* cue) noexcept;
    Cue* cueFor(const std::string& asset_path);
    static bool decodeWav(const std::vector<std::uint8_t>& wav, std::vector<std::uint8_t>* pcm);
    std::unordered_map<std::string, std::unique_ptr<Cue>> cues_;

    SLObjectItf engine_object_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLObjectItf output_mix_object_ = nullptr;
    SLObjectItf player_object_ = nullptr;
    SLPlayItf player_ = nullptr;
    SLAndroidSimpleBufferQueueItf queue_ = nullptr;
    std::vector<std::uint8_t> pcm_;
};

#endif
