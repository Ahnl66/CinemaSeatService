#include "bounded_queue.hpp"
#include "codec_pipeline.hpp"
#include "config.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <source_location>
#include <filesystem>
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/intreadwrite.h>
}
using namespace csad;
static void test(bool b, std::source_location at = std::source_location::current()) { if (!b) throw std::runtime_error("test assertion failed at line " + std::to_string(at.line())); }
template<class F> static void rejects(F f, const char* code = nullptr) {
    try { f(); } catch (const Failure& e) { if (code) test(e.what() == std::string(code)); return; }
    throw std::runtime_error("expected rejection");
}
static void pure(const std::string& fixture_dir) {
    std::ifstream file(fixture_dir + "/input-negative-pts.hex"); std::string hex; file >> hex;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) bytes.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    auto p = parse_packet(bytes, 1, 1, 0); test(p.pts == -1 && p.duration == 1024 && serialize(p) == bytes);
    for (size_t i : {0, 4, 5, 6, 8, 16, 48, 60}) { auto bad = bytes; bad[i] ^= 128; rejects([&] { parse_packet(bad, 1, 1, 0); }); }
    for (size_t n = 0; n < bytes.size(); ++n) rejects([&] { parse_packet(std::span(bytes).first(n), 1, 1, 0); });
    p.pts = INT64_MIN; p.dts = INT64_MAX; test(parse_packet(serialize(p), 1, 1, 0).pts == INT64_MIN);
    for (std::string s : {"{\"a\":1,\"a\":2}", "{\"a\":NaN}", "{\"a\":Infinity}", "[]", "{\"a\":{\"b\":1,\"b\":2}}"}) rejects([&] { parse_json(s); });
    rejects([] { parse_json("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":1}}}}}}}}}"); });
    test(decimal<uint64_t>(Json("18446744073709551615")) == UINT64_MAX);
    test(decimal<int64_t>(Json("-9223372036854775808")) == INT64_MIN);
    for (std::string s : {"+1", "01", "-0", " 1", "1.0", "18446744073709551616", "-1"}) rejects([&] { decimal<uint64_t>(Json(s)); });
    Credit c{0, 100}; c.spend(100); test(!c.fits(1)); c.grant(200); c.spend(100);
    rejects([&] { c.grant(199); }); rejects([] { add(UINT64_MAX, 1); });
    std::vector<uint8_t> data{0, 1, 2, 255}; test(unbase64(base64(data)) == data);
    for (std::string s : {"AA=", "AA==AAAA", "AB==", "____"}) rejects([&] { unbase64(s); });
    auto config = Config::parse({{"version", 1}}); test(config.get("listen", "port") == 9078);
    rejects([] { Config::parse({{"version", 1}, {"unknown", true}}); });
    rejects([] { Config::parse({{"version", 1}, {"sessions", {{"maximum", true}}}}); });
    rejects([] { Config::parse({{"version", 1}, {"listen", {{"port", 65536}}}}); });
    rejects([] { Config::parse({{"version", 1}, {"limits", {{"input_window_bytes", 20}}}}); });
    BoundedQueue<int> q(4, 1); test(q.push(1, 4));
    auto wait = std::async(std::launch::async, [&] { return q.push(2, 4); });
    test(wait.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    q.cancel(); test(!wait.get() && !q.pop());
    BoundedQueue<int> empty(4);
    auto reader = std::async(std::launch::async, [&] { return empty.pop(); });
    empty.cancel(); test(!reader.get());
}
static std::vector<int32_t> canonical(AVFrame* frame) {
    std::vector<int32_t> result;
    int channels = frame->ch_layout.nb_channels;
    auto fmt = static_cast<AVSampleFormat>(frame->format);
    int size = av_get_bytes_per_sample(fmt); bool planar = av_sample_fmt_is_planar(fmt);
    test(av_get_packed_sample_fmt(fmt) == AV_SAMPLE_FMT_S16 || av_get_packed_sample_fmt(fmt) == AV_SAMPLE_FMT_S32);
    for (int i = 0; i < frame->nb_samples; ++i) for (int ch = 0; ch < channels; ++ch) {
        auto* src = frame->extended_data[planar ? ch : 0] + size * (planar ? i : i * channels + ch);
        int32_t n;
        if (size == 2) { int16_t v; std::memcpy(&v, src, 2); n = int32_t(v) * 65536; }
        else std::memcpy(&n, src, 4);
        result.push_back(n);
    }
    return result;
}
static std::vector<Packet> generate(AVCodecID id, int channels, int rate) {
    const AVCodec* codec = avcodec_find_encoder(id); test(codec);
    CodecPtr enc(avcodec_alloc_context3(codec)); test(bool(enc));
    if (id == AV_CODEC_ID_DTS) enc->bit_rate = 768000;
    enc->sample_rate = rate; enc->time_base = {1, rate}; enc->thread_count = 1;
    enc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    enc->sample_fmt = id == AV_CODEC_ID_DTS ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S32P;
    av_channel_layout_default(&enc->ch_layout, channels);
    if (id == AV_CODEC_ID_MLP && channels == 6) { av_channel_layout_uninit(&enc->ch_layout); av_channel_layout_from_mask(&enc->ch_layout, AV_CH_LAYOUT_5POINT1_BACK); }
    test(avcodec_open2(enc.get(), codec, nullptr) >= 0);
    std::vector<Packet> packets;
    auto receive = [&] {
        PacketPtr p(av_packet_alloc());
        while (avcodec_receive_packet(enc.get(), p.get()) >= 0) {
            if (p->size) {
                Packet out; out.sequence = packets.size(); out.flags = 3;
                out.pts = p->pts + rate * 30; out.dts = p->dts + rate * 30; out.duration = p->duration;
                out.payload.assign(p->data, p->data + p->size); packets.push_back(std::move(out));
            }
            av_packet_unref(p.get());
        }
    };
    for (int k = 0; k < 300; ++k) {
        FramePtr frame(av_frame_alloc()); frame->format = enc->sample_fmt; frame->sample_rate = rate;
        frame->nb_samples = enc->frame_size; av_channel_layout_copy(&frame->ch_layout, &enc->ch_layout);
        test(av_frame_get_buffer(frame.get(), 0) >= 0); frame->pts = int64_t(k) * enc->frame_size;
        for (int ch = 0; ch < channels; ++ch) for (int i = 0; i < frame->nb_samples; ++i) {
            // Distinct channel impulses and signed low-bit content detect permutation/truncation.
            int32_t value = ((i + k * 17 + ch * 101) % 1009 - 504) * 256;
            if (enc->sample_fmt == AV_SAMPLE_FMT_FLTP) reinterpret_cast<float*>(frame->data[ch])[i] = value / 2147483648.0f;
            else if (av_sample_fmt_is_planar(enc->sample_fmt)) reinterpret_cast<int32_t*>(frame->data[ch])[i] = value;
            else reinterpret_cast<int32_t*>(frame->data[0])[i * channels + ch] = value;
        }
        test(avcodec_send_frame(enc.get(), frame.get()) >= 0); receive();
    }
    test(avcodec_send_frame(enc.get(), nullptr) >= 0); receive(); test(!packets.empty()); return packets;
}
static std::vector<int32_t> decode(AVCodecID id, const std::vector<Packet>& packets, const Json& format = {}) {
    CodecPtr dec(avcodec_alloc_context3(avcodec_find_decoder(id))); test(bool(dec));
    dec->flags |= AV_CODEC_FLAG_BITEXACT; dec->err_recognition = AV_EF_EXPLODE;
    if (!format.empty()) {
        auto extra = unbase64(format.at("extradata_base64"));
        dec->extradata = static_cast<uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        std::memcpy(dec->extradata, extra.data(), extra.size()); dec->extradata_size = extra.size();
    }
    test(avcodec_open2(dec.get(), dec->codec, nullptr) >= 0);
    std::vector<int32_t> pcm;
    auto receive = [&] {
        FramePtr frame(av_frame_alloc()); int r;
        while ((r = avcodec_receive_frame(dec.get(), frame.get())) >= 0) {
            auto values = canonical(frame.get()); pcm.insert(pcm.end(), values.begin(), values.end()); av_frame_unref(frame.get());
        }
        test(r == AVERROR_EOF || r == AVERROR(EAGAIN));
    };
    for (const auto& p : packets) {
        PacketPtr packet(av_packet_alloc()); test(av_new_packet(packet.get(), p.payload.size()) >= 0);
        std::memcpy(packet->data, p.payload.data(), p.payload.size()); packet->pts = p.flags & 1 ? p.pts : AV_NOPTS_VALUE;
        if (p.skip_start || p.skip_end) {
            auto* side = av_packet_new_side_data(packet.get(), AV_PKT_DATA_SKIP_SAMPLES, 10);
            test(side); AV_WL32(side, p.skip_start); AV_WL32(side + 4, p.skip_end); side[8] = side[9] = 0;
        }
        test(avcodec_send_packet(dec.get(), packet.get()) >= 0); receive();
    }
    test(avcodec_send_packet(dec.get(), nullptr) >= 0); receive(); return pcm;
}
static std::string export_dir;
static void roundtrip(AVCodecID id, const char* codec, int channels, int rate, const std::string& variant = "base") {
    auto input = generate(id, channels, rate);
    if (variant == "negative") for (auto& p : input) { p.pts -= rate * 31; p.dts -= rate * 31; }
    if (variant == "missing-after-anchor") for (size_t i = 100; i < input.size(); ++i) { input[i].flags = 0; input[i].pts = input[i].dts = 0; }
    if (variant == "skip-samples") { input.front().skip_start = 5; input.back().skip_end = 7; }
    auto reference = decode(id, input);
    Json start = {{"type", "start"}, {"protocol", 1}, {"generation", "1"},
        {"input", {{"codec", codec}, {"time_base", {{"num", 1}, {"den", rate}}}, {"extradata_base64", ""},
                   {"sample_rate_hint", nullptr}, {"bits_per_sample_hint", nullptr}, {"channel_order_hint", nullptr}}},
        {"output", {{"codec", "flac"}}}, {"timeline", {{"audible_from_us", "30000000"}}}, {"output_limit_bytes", "4194304"}};
    if (!export_dir.empty() && variant == "base" && channels == 2) {
        Json fixture{{"start", start}, {"packets", Json::array()}};
        for (const auto& packet : input) fixture["packets"].push_back(base64(serialize(packet)));
        std::ofstream(export_dir + "/" + codec + ".json") << fixture.dump();
    }
    Json format; std::vector<Packet> output;
    CodecPipeline pipeline(start, 1, 65536, [&](const Json& f) { test(format.empty()); format = f; },
        [&](Packet p) { test(!format.empty()); output.push_back(std::move(p)); });
    for (const auto& p : input) pipeline.submit(p);
    Json end = pipeline.finish(); test(!format.empty());
    auto actual = decode(AV_CODEC_ID_FLAC, output, format);
    test(actual == reference && !actual.empty());
    uint64_t samples = 0; int64_t next = decimal<int64_t>(end.at("first_pts"));
    for (const auto& p : output) { test(p.pts == next && p.duration > 0); next += p.duration; samples += p.duration; }
    test(samples == decimal<uint64_t>(end.at("total_samples")) && next == decimal<int64_t>(end.at("end_pts_exclusive")));
    test(samples * channels == actual.size());
    bool cancel = true;
    CodecPipeline cancelled(start, 1, 65536, [](auto) {}, [](auto) {}, [&] { return cancel; });
    rejects([&] { cancelled.submit(input.front()); }, "cancelled");
    rejects([&] { cancelled.finish(); }, "cancelled");
    CodecPipeline discontinuity(start, 1, 65536, [](auto) {}, [](auto) {});
    bool failed = false;
    for (auto p : input) {
        if (p.sequence > input.size() / 2) { p.pts += rate; p.flags |= 1; }
        try { discontinuity.submit(p); } catch (const Failure& e) { test(e.what() == std::string("timeline_discontinuity")); failed = true; break; }
    }
    test(failed);
    CodecPipeline missing(start, 1, 65536, [](auto) {}, [](auto) {});
    bool missing_failed = false;
    for (auto p : input) {
        p.flags = 0; p.pts = p.dts = 0;
        try { missing.submit(p); } catch (const Failure& e) { test(e.what() == std::string("missing_timeline")); missing_failed = true; break; }
    }
    test(missing_failed);
    CodecPipeline invalid(start, 1, 65536, [](auto) {}, [](auto) {});
    auto wrong = input.front(); wrong.generation = 2;
    rejects([&] { invalid.submit(wrong); }, "invalid_packet");
    wrong = input.front(); wrong.sequence = 1;
    rejects([&] { invalid.submit(wrong); }, "invalid_packet");
    std::cout << codec << " " << variant << " " << channels << "ch " << rate << "Hz: exact PCM " << samples << " samples; STREAMINFO before data; EOF/timeline/cancel PASS\n";
}
int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_QUIET);
    try {
        test(argc == 2 || argc == 3); if (argc == 3) { export_dir = argv[2]; std::filesystem::create_directories(export_dir); } pure(argv[1]); CodecPipeline::selfcheck();
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000);
        roundtrip(AV_CODEC_ID_MLP, "mlp", 2, 96000);
        roundtrip(AV_CODEC_ID_DTS, "dts", 2, 48000);
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000, "negative");
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000, "missing-after-anchor");
        roundtrip(AV_CODEC_ID_DTS, "dts", 2, 48000, "skip-samples");
        roundtrip(AV_CODEC_ID_MLP, "mlp", 6, 96000);
        std::cout << "core checks PASS (DTS-HD MA hardware fixture remains required)\n";
    } catch (const std::exception& e) { std::cerr << "core checks FAILED: " << e.what() << '\n'; return 1; }
}
