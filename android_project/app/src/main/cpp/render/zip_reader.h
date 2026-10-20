#ifndef WRESTLEFEST_RENDER_ZIP_READER_H
#define WRESTLEFEST_RENDER_ZIP_READER_H

// Reads entries out of an in-memory ZIP. The animation frames live inside
// asset_bundles/Sprites/Character/<name>/wrestler.zip, so the renderer has to
// pull single .sst files out of an archive it already has in memory.

#include <cstdint>
#include <string>
#include <vector>

namespace wrestlefest {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;  // 0 stored, 8 deflated
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
};

class ZipArchive {
public:
    bool open(const std::uint8_t *data, std::size_t size);
    const ZipEntry *find(const std::string &name) const;
    bool read(const std::string &name, std::vector<std::uint8_t> *out);
    bool read(const ZipEntry &entry, std::vector<std::uint8_t> *out);
    const std::vector<ZipEntry> &entries() const { return entries_; }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0;
    std::vector<ZipEntry> entries_;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_RENDER_ZIP_READER_H
