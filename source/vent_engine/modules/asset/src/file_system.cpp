//
// asset module.
// file_system implementation.
// ——————————————————————

#include <_vent/asset/file_system.hpp>
#include <_vent/accessors.hpp>
#include <fstream>

namespace vent {

auto file_system::read_binary(std::string_view path) -> std::vector<u8> {
    std::ifstream file(path.data(), std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        log()->error("asset", "failed to open file: '{}'.", path);
        return {};
    }

    const size_t    fileSize = static_cast<size_t>(file.tellg());
    std::vector<u8> buffer(fileSize);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    return buffer;
}

auto file_system::read_binary_words(std::string_view path) -> std::vector<u32> {
    std::ifstream file(path.data(), std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        log()->error("asset", "failed to open file: '{}'.", path);
        return {};
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());

    if (fileSize % sizeof(u32) != 0) {
        log()->error(
            "asset", "file size is not a multiple of 4 bytes: '{}'.", path);
        return {};
    }

    std::vector<u32> buffer(fileSize / sizeof(u32));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    return buffer;
}

auto file_system::read_text(std::string_view path) -> std::string {
    std::ifstream file(path.data(), std::ios::ate);

    if (!file.is_open()) {
        log()->error("asset", "failed to open file: '{}'.", path);
        return "";
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::string  buffer;
    buffer.resize(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

}  // namespace vent
