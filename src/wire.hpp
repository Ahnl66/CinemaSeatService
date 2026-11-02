#pragma once
#include <nlohmann/json.hpp>
#include <bit>
#include <charconv>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace csad {
using Json = nlohmann::json;
struct Failure : std::runtime_error {
    Json evidence = Json::object();
    explicit Failure(std::string code) : std::runtime_error(std::move(code)) {}
    Failure(std::string code, Json details) : std::runtime_error(std::move(code)), evidence(std::move(details)) {}
};
inline void require(bool condition, const char* code = "protocol_error") {
    if (!condition) throw Failure(code);
}
Json parse_json(std::string_view text, size_t maximum = 65536);
template<class T> T decimal(const Json& j) {
    require(j.is_string());
    const auto& s = j.get_ref<const std::string&>();
    require(!s.empty() && s.size() <= 20);
    T n{};
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    require(error == std::errc{} && end == s.data() + s.size());
    require(std::to_string(n) == s); // canonical decimal, including no negative zero
    return n;
}
inline int integer(const Json& j, int low, int high) {
    require(j.is_number_integer());
    require(j >= low && j <= high);
    return j.get<int>();
}
struct Packet {
    uint8_t kind = 1;
    uint16_t flags = 0;
    uint64_t generation = 1, sequence = 0;
    int64_t pts = 0, dts = 0, duration = 0;
    uint32_t skip_start = 0, skip_end = 0;
    std::vector<uint8_t> payload;
};
Packet parse_packet(std::span<const uint8_t> bytes, uint8_t kind, uint64_t generation,
                    uint64_t sequence, size_t maximum = 1048576);
std::vector<uint8_t> serialize(const Packet& packet);
std::vector<uint8_t> unbase64(const Json& text);
std::string base64(std::span<const uint8_t> bytes);
struct Credit {
    uint64_t sent = 0, limit = 0;
    bool fits(uint64_t size) const { return sent <= limit && size <= limit - sent; }
    void spend(uint64_t size) { require(fits(size), "limit_exceeded"); sent += size; }
    void grant(uint64_t value) { require(value >= limit); limit = value; }
};
inline uint64_t add(uint64_t a, uint64_t b) {
    require(b <= UINT64_MAX - a, "limit_exceeded"); return a + b;
}
}
