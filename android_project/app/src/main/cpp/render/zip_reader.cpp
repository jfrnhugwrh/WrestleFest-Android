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

}  // namespace

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

namespace {

// Finds the End Of Central Directory record in the last 64 KB of `tail`;
// `tail_offset` is the absolute file offset of tail[0].
std::size_t findEocd(const std::uint8_t *tail, std::size_t tail_size) {
    if (tail_size < 22) return std::string::npos;
    for (std::size_t pos = tail_size - 22 + 1; pos-- > 0;) {
        if (readU32(tail + pos) == 0x06054b50u) return pos;
        if (pos == 0) break;
    }
    return std::string::npos;
}

}  // namespace

bool ZipArchive::open(const std::uint8_t *data, std::size_t size) {
    data_ = data;
    size_ = size;
    entries_.clear();
    stream_ = nullptr;
    if (size < 22) return false;

    // Scan backwards for the End Of Central Directory signature.
    const std::size_t search_start = size > 66000 ? size - 66000 : 0;
    const std::size_t eocd = findEocd(data + search_start, size - search_start);
    if (eocd == std::string::npos) return false;
    const std::size_t eocd_offset = search_start + eocd;
    const std::uint32_t directory_offset = readU32(data + eocd_offset + 16);
    if (directory_offset >= size) return false;
    return parseDirectory(data + directory_offset, size - directory_offset, directory_offset) &&
           !entries_.empty();
}

bool ZipArchive::openStreaming(std::size_t total_size, const RangeReader &reader) {
    entries_.clear();
    data_ = nullptr;
    size_ = total_size;
    stream_ = reader;
    if (total_size < 22 || !reader) return false;

    const std::size_t tail_size = std::min<std::size_t>(total_size, 64 * 1024);
    const std::size_t tail_offset = total_size - tail_size;
    std::vector<std::uint8_t> tail(tail_size);
    if (!reader(tail_offset, tail_size, tail.data())) return false;
    const std::size_t eocd = findEocd(tail.data(), tail.size());
    if (eocd == std::string::npos) return false;

    const std::uint16_t count = readU16(tail.data() + eocd + 10);
    const std::uint32_t directory_offset = readU32(tail.data() + eocd + 16);
    const std::uint32_t directory_size = readU32(tail.data() + eocd + 12);
    if (directory_size == 0 || directory_offset + directory_size > total_size) return false;

    std::vector<std::uint8_t> directory(directory_size);
    if (!reader(directory_offset, directory_size, directory.data())) return false;
    if (!parseDirectory(directory.data(), directory.size(), directory_offset)) return false;
    if (entries_.size() < count) {
        // A truncated directory still yields the entries that were parsed.
        return !entries_.empty();
    }
    return true;
}

bool ZipArchive::readStreaming(const ZipEntry &entry, std::vector<std::uint8_t> *out) const {
    if (out == nullptr || !stream_) return false;
    std::uint8_t header[30];
    if (!stream_(entry.local_header_offset, sizeof(header), header)) return false;
    if (readU32(header) != 0x04034b50u) return false;
    const std::uint16_t name_length = readU16(header + 26);
    const std::uint16_t extra_length = readU16(header + 28);
    const std::size_t data_offset =
        static_cast<std::size_t>(entry.local_header_offset) + 30 + name_length + extra_length;

    if (entry.method == 0) {
        out->resize(entry.uncompressed_size);
        return entry.uncompressed_size == 0 ||
               stream_(data_offset, entry.uncompressed_size, out->data());
    }
    if (entry.method != 8) return false;
    std::vector<std::uint8_t> compressed(entry.compressed_size);
    if (entry.compressed_size > 0 &&
        !stream_(data_offset, entry.compressed_size, compressed.data())) {
        return false;
    }
    out->resize(entry.uncompressed_size);
    return inflateRaw(compressed.data(), compressed.size(), out->data(), out->size());
}

bool ZipArchive::parseDirectory(const std::uint8_t *directory, std::size_t directory_size,
                               std::size_t /*directory_offset*/) {
    std::size_t pos = 0;
    const std::size_t count = directory_size / 46 + 1;
    for (std::size_t i = 0; i < count; ++i) {
        if (pos + 46 > directory_size ||
            readU32(directory + pos) != 0x02014b50u) {
            break;
        }
        ZipEntry entry;
        entry.method = readU16(directory + pos + 10);
        entry.compressed_size = readU32(directory + pos + 20);
        entry.uncompressed_size = readU32(directory + pos + 24);
        const std::uint16_t name_length = readU16(directory + pos + 28);
        const std::uint16_t extra_length = readU16(directory + pos + 30);
        const std::uint16_t comment_length = readU16(directory + pos + 32);
        entry.local_header_offset = readU32(directory + pos + 42);
        if (pos + 46 + name_length > directory_size) break;
        entry.name.assign(reinterpret_cast<const char *>(directory + pos + 46), name_length);
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
