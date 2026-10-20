#include "obb_archive.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "WrestleFestObb"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)                 \
    do                            \
    {                             \
        std::printf("[INFO] ");   \
        std::printf(__VA_ARGS__); \
        std::printf("\n");        \
    } while (0)
#define LOGE(...)                          \
    do                                     \
    {                                      \
        std::fprintf(stderr, "[ERROR] ");  \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
    } while (0)
#endif

namespace
{

    constexpr std::uint32_t kEocdSignature = 0x06054b50;
    constexpr std::uint32_t kCentralDirSignature = 0x02014b50;
    constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
    constexpr std::size_t kMaxEocdSearch = 65536 + 22;

    inline int seekFile(std::FILE *f, std::int64_t offset, int whence)
    {
#if defined(_WIN32)
        return _fseeki64(f, offset, whence);
#else
        return fseeko(f, static_cast<off_t>(offset), whence);
#endif
    }

    inline std::int64_t tellFile(std::FILE *f)
    {
#if defined(_WIN32)
        return _ftelli64(f);
#else
        return ftello(f);
#endif
    }

    std::uint16_t readU16(const std::uint8_t *p)
    {
        return static_cast<std::uint16_t>(p[0]) |
               static_cast<std::uint16_t>(p[1] << 8U);
    }

    std::uint32_t readU32(const std::uint8_t *p)
    {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) |
               (static_cast<std::uint32_t>(p[3]) << 24U);
    }

} // namespace

ObbArchive::ObbArchive() = default;

ObbArchive::~ObbArchive()
{
    close();
}

void ObbArchive::close()
{
    if (fd_ >= 0)
    {
        // fd_ is unused if FILE* is used; we store file pointer as void* in fd_ or use FILE* directly
        fd_ = -1;
    }
    entries_.clear();
    file_path_.clear();
}

bool ObbArchive::isOpen() const
{
    return !entries_.empty() && !file_path_.empty();
}

const std::string &ObbArchive::getPath() const
{
    return file_path_;
}

std::size_t ObbArchive::entryCount() const
{
    return entries_.size();
}

std::string ObbArchive::normalizeKey(const std::string &path)
{
    std::string normalized;
    normalized.reserve(path.size());
    for (char c : path)
    {
        if (c == '\\')
        {
            normalized.push_back('/');
        }
        else
        {
            normalized.push_back(c);
        }
    }
    // Strip leading slashes
    std::size_t start = 0;
    while (start < normalized.size() && normalized[start] == '/')
    {
        ++start;
    }
    if (start > 0)
    {
        normalized = normalized.substr(start);
    }
    return normalized;
}

std::string ObbArchive::findKey(const std::string &path) const
{
    const std::string normalized = normalizeKey(path);
    auto it = entries_.find(normalized);
    if (it != entries_.end())
    {
        return normalized;
    }

    // Try finding key if query didn't include category prefix (or vice versa)
    for (const auto &pair : entries_)
    {
        // If entry is "audio/sfx/Attack01.wav" and query is "sfx/Attack01.wav"
        if (pair.first.size() > normalized.size() &&
            pair.first.compare(pair.first.size() - normalized.size(), normalized.size(), normalized) == 0 &&
            pair.first[pair.first.size() - normalized.size() - 1] == '/')
        {
            return pair.first;
        }
    }

    // Case-insensitive fallback
    std::string lower_query = normalized;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    for (const auto &pair : entries_)
    {
        std::string lower_entry = pair.first;
        std::transform(lower_entry.begin(), lower_entry.end(), lower_entry.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        if (lower_entry == lower_query)
        {
            return pair.first;
        }
    }

    return "";
}

bool ObbArchive::hasEntry(const std::string &entry_path) const
{
    return !findKey(entry_path).empty();
}

bool ObbArchive::open(const std::string &path)
{
    close();

    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        LOGE("Failed to open OBB file at '%s'", path.c_str());
        return false;
    }

    if (seekFile(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const std::int64_t file_size = tellFile(file);
    if (file_size < 22)
    {
        LOGE("OBB file '%s' is too small to be a valid zip archive (%lld bytes)", path.c_str(),
             static_cast<long long>(file_size));
        std::fclose(file);
        return false;
    }

    const std::size_t search_size = static_cast<std::size_t>(
        std::min<std::int64_t>(file_size, static_cast<std::int64_t>(kMaxEocdSearch)));
    const std::int64_t search_offset = file_size - static_cast<std::int64_t>(search_size);

    if (seekFile(file, search_offset, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    std::vector<std::uint8_t> search_buffer(search_size);
    if (std::fread(search_buffer.data(), 1, search_size, file) != search_size)
    {
        std::fclose(file);
        return false;
    }

    std::int64_t eocd_pos_in_buffer = -1;
    for (std::int64_t i = static_cast<std::int64_t>(search_size) - 22; i >= 0; --i)
    {
        if (readU32(&search_buffer[static_cast<std::size_t>(i)]) == kEocdSignature)
        {
            eocd_pos_in_buffer = i;
            break;
        }
    }

    if (eocd_pos_in_buffer < 0)
    {
        LOGE("Could not locate End of Central Directory in OBB '%s'", path.c_str());
        std::fclose(file);
        return false;
    }

    const std::uint8_t *eocd = search_buffer.data() + eocd_pos_in_buffer;
    const std::uint16_t total_entries = readU16(eocd + 10);
    const std::uint32_t cd_size = readU32(eocd + 12);
    const std::uint32_t cd_offset = readU32(eocd + 16);

    if (seekFile(file, cd_offset, SEEK_SET) != 0)
    {
        LOGE("Failed to seek to Central Directory at %u in '%s'", cd_offset, path.c_str());
        std::fclose(file);
        return false;
    }

    std::vector<std::uint8_t> cd_buffer(cd_size);
    if (std::fread(cd_buffer.data(), 1, cd_size, file) != cd_size)
    {
        LOGE("Failed to read Central Directory (%u bytes) in '%s'", cd_size, path.c_str());
        std::fclose(file);
        return false;
    }

    std::fclose(file);
    file = nullptr;

    std::size_t offset = 0;
    while (offset + 46 <= cd_size)
    {
        if (readU32(&cd_buffer[offset]) != kCentralDirSignature)
        {
            break;
        }

        const std::uint16_t method = readU16(&cd_buffer[offset + 10]);
        const std::uint32_t crc = readU32(&cd_buffer[offset + 16]);
        const std::uint32_t comp_size = readU32(&cd_buffer[offset + 20]);
        const std::uint32_t uncomp_size = readU32(&cd_buffer[offset + 24]);
        const std::uint16_t fn_len = readU16(&cd_buffer[offset + 28]);
        const std::uint16_t extra_len = readU16(&cd_buffer[offset + 30]);
        const std::uint16_t comment_len = readU16(&cd_buffer[offset + 32]);
        const std::uint32_t local_hdr_offset = readU32(&cd_buffer[offset + 42]);

        if (offset + 46 + fn_len > cd_size)
        {
            break;
        }

        std::string name(reinterpret_cast<const char *>(&cd_buffer[offset + 46]), fn_len);
        name = normalizeKey(name);

        if (!name.empty() && name.back() != '/')
        {
            ZipEntryInfo info{};
            info.name = name;
            info.compression_method = method;
            info.crc32 = crc;
            info.compressed_size = comp_size;
            info.uncompressed_size = uncomp_size;
            info.local_header_offset = local_hdr_offset;
            entries_[name] = info;
        }

        offset += 46 + fn_len + extra_len + comment_len;
    }

    file_path_ = path;
    fd_ = 1; // Mark valid
    LOGI("Opened OBB '%s': indexed %zu entries (expected %u)", path.c_str(), entries_.size(),
         total_entries);
    return true;
}

std::uint64_t ObbArchive::entrySize(const std::string &entry_path) const
{
    const std::string key = findKey(entry_path);
    if (key.empty())
    {
        return 0;
    }
    return entries_.at(key).uncompressed_size;
}

bool ObbArchive::readRange(const std::string &entry_path, std::uint64_t offset, std::size_t length,
                           std::uint8_t *destination) const
{
    if (destination == nullptr || length == 0)
    {
        return true;
    }
    const std::string key = findKey(entry_path);
    if (key.empty())
    {
        return false;
    }
    const ZipEntryInfo &entry = entries_.at(key);
    // Range reads are only meaningful for stored entries (the .obb is written
    // that way); compressed entries have to be read whole.
    if (entry.compression_method != 0 || offset + length > entry.uncompressed_size)
    {
        return false;
    }

    std::FILE *file = std::fopen(file_path_.c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }
    std::uint8_t local_header[30];
    bool ok = seekFile(file, static_cast<std::int64_t>(entry.local_header_offset), SEEK_SET) == 0 &&
              std::fread(local_header, 1, sizeof(local_header), file) == sizeof(local_header) &&
              readU32(local_header) == kLocalHeaderSignature;
    if (ok)
    {
        const std::uint16_t fn_len = readU16(local_header + 26);
        const std::uint16_t extra_len = readU16(local_header + 28);
        const std::int64_t data_offset = static_cast<std::int64_t>(entry.local_header_offset) + 30 +
                                         fn_len + extra_len + static_cast<std::int64_t>(offset);
        ok = seekFile(file, data_offset, SEEK_SET) == 0 &&
             std::fread(destination, 1, length, file) == length;
    }
    std::fclose(file);
    return ok;
}

bool ObbArchive::readFile(const std::string &entry_path, std::vector<std::uint8_t> *bytes)
{
    if (bytes == nullptr || !isOpen())
    {
        return false;
    }

    const std::string key = findKey(entry_path);
    if (key.empty())
    {
        return false;
    }

    const ZipEntryInfo &entry = entries_[key];
    std::FILE *file = std::fopen(file_path_.c_str(), "rb");
    if (file == nullptr)
    {
        LOGE("Failed to open OBB file for reading: %s", file_path_.c_str());
        return false;
    }

    // Read local header to determine data start offset
    if (seekFile(file, static_cast<std::int64_t>(entry.local_header_offset), SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    std::uint8_t local_header[30];
    if (std::fread(local_header, 1, sizeof(local_header), file) != sizeof(local_header))
    {
        std::fclose(file);
        return false;
    }

    if (readU32(local_header) != kLocalHeaderSignature)
    {
        LOGE("Invalid local header signature for entry '%s'", key.c_str());
        std::fclose(file);
        return false;
    }

    const std::uint16_t fn_len = readU16(local_header + 26);
    const std::uint16_t extra_len = readU16(local_header + 28);
    const std::int64_t data_offset = static_cast<std::int64_t>(entry.local_header_offset) + 30 +
                                     fn_len + extra_len;

    if (seekFile(file, data_offset, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    if (entry.compression_method == 0)
    { // STORED
        bytes->resize(static_cast<std::size_t>(entry.uncompressed_size));
        if (entry.uncompressed_size > 0)
        {
            const std::size_t bytes_read =
                std::fread(bytes->data(), 1, bytes->size(), file);
            if (bytes_read != bytes->size())
            {
                std::fclose(file);
                return false;
            }
        }
    }
    else if (entry.compression_method == 8)
    { // DEFLATED
        std::vector<std::uint8_t> compressed(static_cast<std::size_t>(entry.compressed_size));
        if (entry.compressed_size > 0)
        {
            const std::size_t bytes_read =
                std::fread(compressed.data(), 1, compressed.size(), file);
            if (bytes_read != compressed.size())
            {
                std::fclose(file);
                return false;
            }
        }

        bytes->resize(static_cast<std::size_t>(entry.uncompressed_size));
        z_stream strm{};
        strm.next_in = reinterpret_cast<Bytef *>(compressed.data());
        strm.avail_in = static_cast<uInt>(compressed.size());
        strm.next_out = reinterpret_cast<Bytef *>(bytes->data());
        strm.avail_out = static_cast<uInt>(bytes->size());

        // -MAX_WBITS specifies raw DEFLATE without zlib or gzip wrappers
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        {
            LOGE("inflateInit2 failed for '%s'", key.c_str());
            std::fclose(file);
            return false;
        }

        const int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);

        if (ret != Z_STREAM_END && ret != Z_OK)
        {
            LOGE("inflate failed for '%s': code %d", key.c_str(), ret);
            std::fclose(file);
            return false;
        }
    }
    else
    {
        LOGE("Unsupported compression method %u for '%s'", entry.compression_method, key.c_str());
        std::fclose(file);
        return false;
    }

    std::fclose(file);

    // Verify CRC32
    if (entry.crc32 != 0 && !bytes->empty())
    {
        const uLong calc_crc = crc32(
            0L, reinterpret_cast<const Bytef *>(bytes->data()), static_cast<uInt>(bytes->size()));
        if (static_cast<std::uint32_t>(calc_crc) != entry.crc32)
        {
            LOGE("CRC32 mismatch for '%s': expected 0x%08X, got 0x%08lX", key.c_str(), entry.crc32,
                 calc_crc);
            return false;
        }
    }

    return true;
}
