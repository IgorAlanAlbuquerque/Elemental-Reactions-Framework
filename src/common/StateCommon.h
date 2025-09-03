#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#if __has_include(<version>)
    #include <version>
#endif
#if defined(__cpp_lib_to_underlying) && (__cpp_lib_to_underlying >= 202102L)
    #include <utility>
using std::to_underlying;
#else
template <class Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum> >(e);
}
#endif

constexpr std::uint32_t FOURCC(char a, char b, char c, char d) noexcept {
    return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | std::uint32_t(d);
}

constexpr std::uint8_t clamp100(int v) noexcept {
    int result;
    if (v < 0) {
        result = 0;
    } else if (v > 100) {
        result = 100;
    } else {
        result = v;
    }
    return static_cast<std::uint8_t>(result);
}
