#pragma once

#include <cstdint>
#include <cstring>
#include <array>

namespace gnat {

namespace key {
constexpr uint64_t GetChar(const char* decoded, size_t index) {
    return static_cast<uint64_t>(decoded[index]) << 8*index;
}

template<size_t N>
constexpr uint64_t Encode(char const (&decoded)[N]) {
    static_assert(N < 10, "Key too long!");
    uint64_t encoded = 0;
    for (size_t i = 0; i < std::min(N, (size_t)8); i++) {
      encoded |= GetChar(decoded, i);
    }
    return encoded;
}

uint64_t EncodeString(const char* decoded, size_t topic_bytes) {
    uint64_t encoded = 0;
    for (size_t i = 0; i < topic_bytes; i++) {
      encoded |= GetChar(decoded, i);
    }
    return encoded;
}

std::string Decode(uint64_t encoded) {
    char* encoded_str = reinterpret_cast<char*>(&encoded);
    return std::string(encoded_str, std::min((size_t)8, strlen(encoded_str)));
}

// decode_to should be atleast 8 bytes;
void DecodeString(uint64_t encoded, char* decode_to, uint8_t* decoded_size) {
    char* encoded_str = reinterpret_cast<char*>(&encoded);
    *decoded_size = std::min((size_t)8, strlen(encoded_str));
    memcpy(decode_to, encoded_str, *decoded_size);
}

}  // namespace key
}  // namespace gnat

