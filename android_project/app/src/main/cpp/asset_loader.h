#ifndef WRESTLEFEST_ASSET_LOADER_H
#define WRESTLEFEST_ASSET_LOADER_H

#include "obb_archive.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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

    // Random-access read of an asset, used by the renderer to pull single .sst
    // frames out of the 5 MB wrestler zips without loading them into memory.
    // `header` receives the first `header_size` bytes; the callback serves any
    // later range (offset, length, destination).
    using RangeReader =
        std::function<bool(std::size_t offset, std::size_t length, std::uint8_t *destination)>;
    bool openAssetRange(const std::string &path, std::size_t header_size,
                        std::vector<std::uint8_t> *header, RangeReader *reader) const;

    // Size of an asset in bytes (0 when it is not available).
    std::uint64_t assetSize(const std::string &path) const;

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
