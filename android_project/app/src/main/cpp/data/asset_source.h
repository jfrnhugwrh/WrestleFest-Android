#ifndef WRESTLEFEST_DATA_ASSET_SOURCE_H
#define WRESTLEFEST_DATA_ASSET_SOURCE_H

// Abstraction over "where asset bytes come from". The simulation and the data
// layer only need read(); the Android side plugs in AssetLoader (APK assets +
// OBB), the host tests plug in a directory. Keeping this interface here is what
// lets the whole data+sim stack build and run without Android headers.

#include <cstdint>
#include <string>
#include <vector>

namespace wrestlefest {

class AssetSource {
public:
    virtual ~AssetSource() = default;
    virtual bool read(const std::string &path, std::vector<std::uint8_t> *bytes) = 0;
    virtual bool exists(const std::string &path) = 0;
};

// Reads from a directory tree on disk (host tests, desktop tools).
class DirectoryAssetSource : public AssetSource {
public:
    explicit DirectoryAssetSource(std::string root);
    bool read(const std::string &path, std::vector<std::uint8_t> *bytes) override;
    bool exists(const std::string &path) override;
    const std::string &root() const { return root_; }

private:
    std::string pathFor(const std::string &path) const;
    std::string root_;
};

// Reads from an in-memory map (unit tests, generated fixtures).
class MemoryAssetSource : public AssetSource {
public:
    void add(const std::string &path, std::vector<std::uint8_t> bytes);
    void addText(const std::string &path, const std::string &text);
    bool read(const std::string &path, std::vector<std::uint8_t> *bytes) override;
    bool exists(const std::string &path) override;

private:
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files_;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_DATA_ASSET_SOURCE_H
