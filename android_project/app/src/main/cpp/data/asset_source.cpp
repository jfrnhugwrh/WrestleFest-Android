#include "asset_source.h"

#include <cstdio>
#include <utility>

namespace wrestlefest {

DirectoryAssetSource::DirectoryAssetSource(std::string root) : root_(std::move(root)) {
    if (!root_.empty() && root_.back() == '/') root_.pop_back();
}

std::string DirectoryAssetSource::pathFor(const std::string &path) const {
    if (root_.empty() || (!path.empty() && path.front() == '/')) return path;
    return root_ + "/" + path;
}

bool DirectoryAssetSource::read(const std::string &path, std::vector<std::uint8_t> *bytes) {
    if (bytes == nullptr) return false;
    const std::string full = pathFor(path);
    std::FILE *file = std::fopen(full.c_str(), "rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        return false;
    }
    bytes->resize(static_cast<std::size_t>(size));
    const std::size_t read = bytes->empty() ? 0 : std::fread(bytes->data(), 1, bytes->size(), file);
    std::fclose(file);
    return read == bytes->size();
}

bool DirectoryAssetSource::exists(const std::string &path) {
    std::FILE *file = std::fopen(pathFor(path).c_str(), "rb");
    if (file == nullptr) return false;
    std::fclose(file);
    return true;
}

void MemoryAssetSource::add(const std::string &path, std::vector<std::uint8_t> bytes) {
    files_.emplace_back(path, std::move(bytes));
}

void MemoryAssetSource::addText(const std::string &path, const std::string &text) {
    add(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool MemoryAssetSource::read(const std::string &path, std::vector<std::uint8_t> *bytes) {
    if (bytes == nullptr) return false;
    for (const auto &entry : files_) {
        if (entry.first == path) {
            *bytes = entry.second;
            return true;
        }
    }
    return false;
}

bool MemoryAssetSource::exists(const std::string &path) {
    for (const auto &entry : files_) {
        if (entry.first == path) return true;
    }
    return false;
}

}  // namespace wrestlefest
