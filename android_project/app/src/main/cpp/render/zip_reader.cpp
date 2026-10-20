#include "zip_reader.h"

#include <zlib.h>

#include <cstring>

namespace wrestlefest {
namespace {

std::uint16_t readU16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t readU32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool inflateRaw(const std::uint8_t *src, std::size_t src_size, std::uint8_t *dst,
                std::size_t dst_size) {
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    stream.next_in = const_cast<Bytef *>(src);
    stream.avail_in = static_cast<uInt>(src_size);
    stream.next_out = dst;
    stream.avail_out = static_cast<uInt>(dst_size);
    const int rc = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return rc == Z_STREAM_END;
}

}  // namespace

bool ZipArchive::open(const std::uint8_t *data, std::size_t size) {
    data_ = data;
    size_ = size;
    entries_.clear();
    if (size < 22) return false;

    // Scan backwards for the End Of Central Directory signature.
    std::size_t eocd = std::string::npos;
    const std::size_t search_start = size > 66000 ? size - 66000 : 0;
    for (std::size_t pos = size - 22 + 1; pos-- > search_start;) {
        if (readU32(data + pos) == 0x06054b50u) {
            eocd = pos;
            break;
        }
        if (pos == 0) break;
    }
    if (eocd == std::string::npos) return false;

    const std::uint16_t count = readU16(data + eocd + 10);
    const std::uint32_t directory_offset = readU32(data + eocd + 16);
    if (static_cast<std::size_t>(directory_offset) >= size) return false;

    std::size_t pos = directory_offset;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > size || readU32(data + pos) != 0x02014b50u) break;
        ZipEntry entry;
        entry.method = readU16(data + pos + 10);
        entry.compressed_size = readU32(data + pos + 20);
        entry.uncompressed_size = readU32(data + pos + 24);
        const std::uint16_t name_length = readU16(data + pos + 28);
        const std::uint16_t extra_length = readU16(data + pos + 30);
        const std::uint16_t comment_length = readU16(data + pos + 32);
        entry.local_header_offset = readU32(data + pos + 42);
        if (pos + 46 + name_length > size) break;
        entry.name.assign(reinterpret_cast<const char *>(data + pos + 46), name_length);
        entries_.push_back(std::move(entry));
        pos += 46 + name_length + extra_length + comment_length;
    }
    return !entries_.empty();
}

const ZipEntry *ZipArchive::find(const std::string &name) const {
    for (const ZipEntry &entry : entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

bool ZipArchive::read(const std::string &name, std::vector<std::uint8_t> *out) {
    const ZipEntry *entry = find(name);
    if (entry == nullptr) return false;
    return read(*entry, out);
}

bool ZipArchive::read(const ZipEntry &entry, std::vector<std::uint8_t> *out) {
    if (out == nullptr) return false;
    const std::size_t header = static_cast<std::size_t>(entry.local_header_offset);
    if (header + 30 > size_ || readU32(data_ + header) != 0x04034b50u) return false;
    const std::uint16_t name_length = readU16(data_ + header + 26);
    const std::uint16_t extra_length = readU16(data_ + header + 28);
    const std::size_t data_offset = header + 30 + name_length + extra_length;
    if (data_offset + entry.compressed_size > size_) return false;

    if (entry.method == 0) {
        out->assign(data_ + data_offset, data_ + data_offset + entry.uncompressed_size);
        return true;
    }
    if (entry.method != 8) return false;
    out->resize(entry.uncompressed_size);
    if (out->empty()) return true;
    return inflateRaw(data_ + data_offset, entry.compressed_size, out->data(),
                      entry.uncompressed_size);
}

}  // namespace wrestlefest
