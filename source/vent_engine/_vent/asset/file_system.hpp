#pragma once
//
// vent public sdk.
// description.
// ——————————————————————
//
// details.

#include <_vent/vent_sdk.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace vent {

class file_system {
public:
    /// @brief reads a binary file fully into a byte array.
    [[nodiscard]]
    static auto read_binary(std::string_view path) -> std::vector<u8>;

    /// @brief reads a binary file into a word array.
    [[nodiscard]]
    static auto read_binary_words(std::string_view path) -> std::vector<u32>;

    /// @brief reads a text file fully into a string.
    [[nodiscard]]
    static auto read_text(std::string_view path) -> std::string;
};

}  // namespace vent
