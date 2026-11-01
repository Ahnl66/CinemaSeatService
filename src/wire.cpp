#include "wire.hpp"
#include <set>

namespace csad {
Json parse_json(std::string_view text, size_t maximum) {
    require(text.size() <= maximum, "limit_exceeded");
    std::vector<std::set<std::string>> keys;
    try {
        auto result = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
            require(depth <= 8);
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key) require(!keys.empty() && keys.back().insert(value.get<std::string>()).second);
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        require(result.is_object());
        return result;
    } catch (const Json::exception&) { throw Failure("protocol_error"); }
}
static uint64_t load(std::span<const uint8_t> b, size_t offset, int count) {
    uint64_t n = 0;
    for (int i = 0; i < count; ++i) n = (n << 8) | b[offset + i];
    return n;
}
Packet parse_packet(std::span<const uint8_t> b, uint8_t kind, uint64_t generation,
                    uint64_t sequence, size_t maximum) {
    require(b.size() >= 64, "invalid_packet");
    require(load(b, 0, 4) == 0x43534144 && b[4] == 1 && b[5] == kind, "invalid_packet");
    Packet p;
    p.kind = kind; p.flags = load(b, 6, 2);
    p.generation = load(b, 8, 8); p.sequence = load(b, 16, 8);
    p.pts = std::bit_cast<int64_t>(load(b, 24, 8)); p.dts = std::bit_cast<int64_t>(load(b, 32, 8));
    p.duration = std::bit_cast<int64_t>(load(b, 40, 8));
    auto size = load(b, 48, 4);
    p.skip_start = load(b, 52, 4); p.skip_end = load(b, 56, 4);
    require(p.generation == generation && p.sequence == sequence && generation > 0 && generation <= INT64_MAX, "invalid_packet");
    require(!(p.flags & ~7) && p.duration >= 0 && load(b, 60, 4) == 0, "invalid_packet");
    require((p.flags & 1 || p.pts == 0) && (p.flags & 2 || p.dts == 0), "invalid_packet");
    require(size > 0 && size <= maximum && b.size() == 64 + size, "invalid_packet");
    if (kind == 2) require((p.flags & 3) == 3 && p.pts == p.dts && p.duration > 0 && !p.skip_start && !p.skip_end, "invalid_packet");
    p.payload.assign(b.begin() + 64, b.end());
    return p;
}
std::vector<uint8_t> serialize(const Packet& p) {
    require(!p.payload.empty() && p.payload.size() <= 1048576, "limit_exceeded");
    std::vector<uint8_t> b(64 + p.payload.size());
    auto put = [&](size_t off, int count, uint64_t value) {
        for (int i = count - 1; i >= 0; --i) { b[off + i] = value & 255; value >>= 8; }
    };
    put(0, 4, 0x43534144); b[4] = 1; b[5] = p.kind;
    put(6, 2, p.flags); put(8, 8, p.generation); put(16, 8, p.sequence);
    put(24, 8, std::bit_cast<uint64_t>(p.pts)); put(32, 8, std::bit_cast<uint64_t>(p.dts));
    put(40, 8, std::bit_cast<uint64_t>(p.duration)); put(48, 4, p.payload.size());
    put(52, 4, p.skip_start); put(56, 4, p.skip_end);
    std::copy(p.payload.begin(), p.payload.end(), b.begin() + 64); return b;
}
static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(std::span<const uint8_t> bytes) {
    std::string s;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t n = uint32_t(bytes[i]) << 16;
        if (i + 1 < bytes.size()) n |= uint32_t(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) n |= bytes[i + 2];
        s += alphabet[n >> 18]; s += alphabet[(n >> 12) & 63];
        s += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        s += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return s;
}
std::vector<uint8_t> unbase64(const Json& value) {
    require(value.is_string());
    auto s = value.get<std::string>();
    require(s.size() <= 87384 && s.size() % 4 == 0);
    std::vector<uint8_t> out;
    uint32_t n = 0; int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        const char* pos = std::find(alphabet, alphabet + 64, c);
        require(pos != alphabet + 64);
        n = (n << 6) | (pos - alphabet); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((n >> bits) & 255); }
    }
    require(out.size() <= 65536 && base64(out) == s);
    return out;
}
}
