#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "savetinker/byte_buffer.hpp"

namespace test {

inline st::Bytes bytesOf(std::string_view text) {
    return st::Bytes(text.begin(), text.end());
}

inline st::ByteView viewOf(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

inline void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("savetinker-test-" + std::to_string(::time(nullptr)) + "-" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}
