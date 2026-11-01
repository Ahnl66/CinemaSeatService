#include "config.hpp"
#include <boost/asio/ip/address.hpp>
#include <fstream>
namespace csad {
Config Config::defaults() {
    return {Json{
        {"version", 1}, {"listen", {{"address", "0.0.0.0"}, {"port", 9078}}},
        {"sessions", {{"maximum", 4}, {"codec_threads", 1}}},
        {"limits", {{"control_message_bytes", 65536}, {"packet_payload_bytes", 1048576},
          {"input_window_bytes", 4194304}, {"output_window_bytes", 4194304},
          {"queued_input_bytes", 8388608}, {"queued_output_bytes", 8388608}, {"pcm_fifo_samples", 65536}}},
        {"timeouts", {{"handshake_seconds", 5}, {"start_seconds", 5}, {"format_seconds", 10},
          {"ping_seconds", 15}, {"pong_seconds", 10}, {"drain_seconds", 10}, {"close_seconds", 2}}},
        {"logging", {{"debug_enabled", false}, {"file", "/logs/decoder.log"},
          {"maximum_file_bytes", 10485760}, {"retained_files", 3}}}
    }};
}
static void merge(Json& target, const Json& source) {
    require(source.is_object());
    for (auto& [key, value] : source.items()) {
        require(target.contains(key), "unknown_setting");
        auto& old = target[key];
        if (old.is_object()) merge(old, value);
        else {
            require((old.is_number_integer() && value.is_number_integer()) || old.type() == value.type());
            old = value;
        }
    }
}
Config Config::parse(const Json& input) {
    require(input.contains("version") && integer(input.at("version"), 1, 1) == 1);
    auto c = defaults(); merge(c.value, input);
    auto range = [&](const char* group, const char* key, int lo, int hi) { integer(c.value.at(group).at(key), lo, hi); };
    range("listen", "port", 1, 65535);
    boost::system::error_code error;
    boost::asio::ip::make_address(c.value["listen"]["address"].get<std::string>(), error);
    require(!error);
    range("sessions", "maximum", 1, 32); range("sessions", "codec_threads", 1, 4);
    range("limits", "control_message_bytes", 1024, 65536);
    range("limits", "packet_payload_bytes", 1, 1048576);
    range("limits", "pcm_fifo_samples", 8192, 262144);
    for (const char* direction : {"input", "output"}) {
        std::string window = std::string(direction) + "_window_bytes", queue = std::string("queued_") + direction + "_bytes";
        range("limits", queue.c_str(), 128, 64 * 1024 * 1024);
        range("limits", window.c_str(), c.get("limits", "packet_payload_bytes") + 64, c.get("limits", queue.c_str()) - c.get("limits", "packet_payload_bytes") - 64);
    }
    for (auto& [key, value] : c.value["timeouts"].items()) integer(value, key == "ping_seconds" ? 5 : 1, key == "ping_seconds" ? 60 : 120);
    range("logging", "maximum_file_bytes", 1024, 100 * 1024 * 1024);
    range("logging", "retained_files", 1, 10);
    require(!c.value["logging"]["file"].get<std::string>().empty());
    return c;
}
Config Config::read(const std::string& path) {
    std::ifstream file(path, std::ios::binary); require(file.good(), "settings_file_unreadable");
    std::string text(65537, '\0'); file.read(text.data(), text.size()); text.resize(file.gcount());
    return parse(parse_json(text));
}
}
