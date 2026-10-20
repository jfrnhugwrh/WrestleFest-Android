#ifndef WRESTLEFEST_RENDER_ZIP_READER_H
#define WRESTLEFEST_RENDER_ZIP_READER_H

// Reads entries out of an in-memory ZIP. The animation frames live inside
// asset_bundles/Sprites/Character/<name>/wrestler.zip, so the renderer has to
// pull single .sst files out of an archive it already has in memory.

#include <cstdint>
#include <functional>
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
    // Serves any byte range of the archive. Used for bundles that are too big to
    // keep in memory (a wrestler zip is ~5 MB and only a few frames are needed
    // per second).
    using RangeReader = std::function<bool(std::size_t offset, std::size_t length,
                                           std::uint8_t *destination)>;

    bool open(const std::uint8_t *data, std::size_t size);
    // Reads the central directory from the end of the archive; the payload is
    // fetched later with readStreaming().
    bool openStreaming(std::size_t total_size, const RangeReader &reader);
    // Extracts one entry using the reader that was passed to openStreaming().
    bool readStreaming(const ZipEntry &entry, std::vector<std::uint8_t> *out) const;
    const RangeReader &stream() const { return stream_; }
    const ZipEntry *find(const std::string &name) const;
    bool read(const std::string &name, std::vector<std::uint8_t> *out);
    bool read(const ZipEntry &entry, std::vector<std::uint8_t> *out);
    const std::vector<ZipEntry> &entries() const { return entries_; }


private:
    bool parseDirectory(const std::uint8_t *directory, std::size_t directory_size,
                        std::size_t directory_offset);

    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0;
    std::vector<ZipEntry> entries_;
    RangeReader stream_;
};

// Inflates a raw deflate stream (no zlib/gzip wrapper). Exposed so callers that
// stream archived data can decode a single entry without a ZipArchive object.
bool inflateRaw(const std::uint8_t *src, std::size_t src_size, std::uint8_t *dst,
                std::size_t dst_size);

}  // namespace wrestlefest

#endif  // WRESTLEFEST_RENDER_ZIP_READER_H
