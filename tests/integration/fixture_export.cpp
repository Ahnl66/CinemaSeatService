// Host-only, codec-gated fixture extraction. Never linked into the service.
#include "wire.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>
}
using namespace csad;
struct FormatFree { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct InputPacketFree { void operator()(AVPacket* p) const { av_packet_free(&p); } };
int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_QUIET);
    try {
        require(argc == 4, "invalid_arguments");
        int ordinal = std::stoi(argv[2]), seconds = std::stoi(argv[3]);
        require(ordinal >= 0 && ordinal < 128 && seconds >= 1 && seconds <= 15);
        std::string path = argv[1]; require(!path.empty() && path[0] == '/', "local_source_required");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        AVFormatContext* raw = avformat_alloc_context(); require(raw, "allocation_failed");
        raw->interrupt_callback = {[](void* value) -> int { return std::chrono::steady_clock::now() >= *static_cast<std::chrono::steady_clock::time_point*>(value); }, &deadline};
        AVDictionary* options = nullptr; av_dict_set(&options, "protocol_whitelist", "file", 0);
        av_dict_set(&options, "probesize", "1048576", 0); av_dict_set(&options, "analyzeduration", "2000000", 0);
        int status = avformat_open_input(&raw, path.c_str(), nullptr, &options); av_dict_free(&options);
        std::unique_ptr<AVFormatContext, FormatFree> input(raw); require(status >= 0, "source_open_failed");
        require(avformat_find_stream_info(input.get(), nullptr) >= 0, "source_probe_failed");
        AVStream* stream = nullptr;
        for (unsigned i = 0; i < input->nb_streams; ++i) if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && ordinal-- == 0) { stream = input->streams[i]; break; }
        require(stream, "audio_ordinal_out_of_range");
        auto* parameters = stream->codecpar;
        require(parameters->extradata_size >= 0 && parameters->extradata_size <= 65536, "extradata_limit");
        std::string codec = parameters->codec_id == AV_CODEC_ID_DTS ? "dts" : parameters->codec_id == AV_CODEC_ID_TRUEHD ? "truehd" : parameters->codec_id == AV_CODEC_ID_MLP ? "mlp" : "";
        require(!codec.empty(), "source_gate_unsupported_codec");
        Json order = nullptr;
        if (parameters->ch_layout.order == AV_CHANNEL_ORDER_NATIVE && av_channel_layout_check(&parameters->ch_layout)) {
            order = Json::array();
            for (int ch = 0; ch < parameters->ch_layout.nb_channels; ++ch) {
                char name[16]; require(av_channel_name(name, sizeof(name), av_channel_layout_channel_from_index(&parameters->ch_layout, ch)) >= 0);
                order.push_back(name);
            }
        }
        Json start{{"type", "start"}, {"protocol", 1}, {"generation", "1"},
            {"input", {{"codec", codec}, {"time_base", {{"num", stream->time_base.num}, {"den", stream->time_base.den}}},
                {"extradata_base64", base64({parameters->extradata, static_cast<size_t>(parameters->extradata_size)})},
                {"sample_rate_hint", parameters->sample_rate > 0 ? Json(parameters->sample_rate) : Json(nullptr)},
                {"bits_per_sample_hint", parameters->bits_per_raw_sample > 0 ? Json(parameters->bits_per_raw_sample) : Json(nullptr)},
                {"channel_order_hint", order}}},
            {"output", {{"codec", "flac"}}}, {"timeline", {{"audible_from_us", "0"}}}, {"output_limit_bytes", "4194304"}};
        Json fixture{{"start", start}, {"packets", Json::array()}, {"demux_positions", Json::array()}};
        if (parameters->profile == AV_PROFILE_DTS_HD_MA || parameters->profile == AV_PROFILE_DTS_HD_MA_X || parameters->profile == AV_PROFILE_DTS_HD_MA_X_IMAX) fixture["expected_source_profile"] = "dts-hd-ma";
        std::unique_ptr<AVPacket, InputPacketFree> packet(av_packet_alloc()); require(bool(packet), "allocation_failed");
        size_t bytes = 0; std::optional<int64_t> first;
        while ((status = av_read_frame(input.get(), packet.get())) >= 0) {
            if (packet->stream_index != stream->index) { av_packet_unref(packet.get()); continue; }
            require(!(packet->flags & (AV_PKT_FLAG_CORRUPT | AV_PKT_FLAG_DISCARD)), "invalid_source_packet");
            require(packet->size > 0 && packet->size <= 1048576 && fixture["packets"].size() < 4096, "fixture_limit");
            bytes += packet->size + 64; require(bytes <= 24 * 1024 * 1024, "fixture_limit");
            Packet p; p.sequence = fixture["packets"].size();
            if (packet->pts != AV_NOPTS_VALUE) { p.flags |= 1; p.pts = packet->pts; if (!first) first = p.pts; }
            if (packet->dts != AV_NOPTS_VALUE) { p.flags |= 2; p.dts = packet->dts; }
            if (packet->flags & AV_PKT_FLAG_KEY) p.flags |= 4;
            p.duration = packet->duration;
            for (int side = 0; side < packet->side_data_elems; ++side) {
                auto& data = packet->side_data[side];
                bool irrelevant = data.type == AV_PKT_DATA_REPLAYGAIN || data.type == AV_PKT_DATA_STRINGS_METADATA ||
                    data.type == AV_PKT_DATA_METADATA_UPDATE || data.type == AV_PKT_DATA_AUDIO_SERVICE_TYPE ||
                    data.type == AV_PKT_DATA_QUALITY_STATS || data.type == AV_PKT_DATA_CPB_PROPERTIES || data.type == AV_PKT_DATA_PRFT;
                require(data.type == AV_PKT_DATA_SKIP_SAMPLES || irrelevant, "unsupported_side_data");
                if (data.type == AV_PKT_DATA_SKIP_SAMPLES) { require(data.size >= 10); p.skip_start = AV_RL32(data.data); p.skip_end = AV_RL32(data.data + 4); }
            }
            p.payload.assign(packet->data, packet->data + packet->size); fixture["packets"].push_back(base64(serialize(p))); fixture["demux_positions"].push_back(std::to_string(packet->pos));
            av_packet_unref(packet.get());
            if (first && p.flags & 1 && (static_cast<long double>(p.pts) - *first) * av_q2d(stream->time_base) >= seconds) break;
        }
        require(status == AVERROR_EOF || status >= 0, "source_read_failed"); require(!fixture["packets"].empty(), "empty_fixture");
        // stdout is a transient RAM pipe into the test client, not an evidence report.
        std::cout << fixture.dump() << '\n';
    } catch (const Failure& error) { std::cerr << error.what() << '\n'; return 1; }
      catch (...) { std::cerr << "fixture_export_failed\n"; return 1; }
}
