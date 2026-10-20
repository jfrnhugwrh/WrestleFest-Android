#ifndef WRESTLEFEST_OBB_ARCHIVE_H
#define WRESTLEFEST_OBB_ARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ZipEntryInfo {
    std::string name;
    std::uint16_t compression_method = 0;  // 0 = Stored, 8 = Deflated
    std::uint32_t crc32 = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t local_header_offset = 0;
};

class ObbArchive {
public:
    ObbArchive();
    ~ObbArchive();

    ObbArchive(const ObbArchive&) = delete;
    ObbArchive& operator=(const ObbArchive&) = delete;

    bool open(const std::string& path);
    void close();

    bool isOpen() const;
    const std::string& getPath() const;
    std::size_t entryCount() const;

    bool hasEntry(const std::string& entry_path) const;
    bool readFile(const std::string& entry_path, std::vector<std::uint8_t>* bytes);

    static std::string normalizeKey(const std::string& path);

private:
    std::string findKey(const std::string& path) const;

    std::string file_path_;
    int fd_ = -1;
    std::unordered_map<std::string, ZipEntryInfo> entries_;
};

#endif  // WRESTLEFEST_OBB_ARCHIVE_H
