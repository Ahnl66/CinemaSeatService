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
#include <libavutil/sha.h>
}
using namespace csad;
static std::atomic<uint64_t> observed_xll_recovery{0};
static void policy_log(void*, int, const char* format, va_list) {
    // Recognize only a fixed upstream marker. Never format or retain raw logs.
    if (std::strcmp(format, "Forcing XLL recovery mode\n") == 0) ++observed_xll_recovery;
}

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
    if (id == AV_CODEC_ID_DTS && channels == 6) { av_channel_layout_uninit(&enc->ch_layout); av_channel_layout_from_mask(&enc->ch_layout, AV_CH_LAYOUT_5POINT1); }
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
static std::vector<int32_t> decode(AVCodecID id, const std::vector<Packet>& packets, const Json& format = {}, Json* measured = nullptr, AVRational source_tb = {0, 1}, Json* trace = nullptr) {
    CodecPtr dec(avcodec_alloc_context3(avcodec_find_decoder(id))); test(bool(dec));
    dec->flags |= AV_CODEC_FLAG_BITEXACT; dec->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE; dec->pkt_timebase = source_tb;
    if (!format.empty()) {
        // FLAC STREAMINFO does not encode side versus back speaker identity.
        // Restore the explicit wire layout before opening the reference decoder.
        if (id == AV_CODEC_ID_FLAC && format.contains("channel_order")) {
            uint64_t mask = 0; const auto& order = format.at("channel_order"); test(order.is_array() && !order.empty() && order.size() <= 8);
            for (const auto& name : order) {
                test(name.is_string()); auto channel = av_channel_from_string(name.get<std::string>().c_str());
                test(channel >= 0 && channel < 64 && !(mask & (uint64_t(1) << channel))); mask |= uint64_t(1) << channel;
            }
            test(av_channel_layout_from_mask(&dec->ch_layout, mask) >= 0);
            for (size_t ch = 0; ch < order.size(); ++ch) {
                char name[16]; test(av_channel_name(name, sizeof(name), av_channel_layout_channel_from_index(&dec->ch_layout, ch)) >= 0); test(order[ch] == name);
            }
        }
        auto extra = unbase64(format.at("extradata_base64"));
        dec->extradata = static_cast<uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        std::memcpy(dec->extradata, extra.data(), extra.size()); dec->extradata_size = extra.size();
    }
    test(avcodec_open2(dec.get(), dec->codec, nullptr) >= 0);
    std::vector<int32_t> pcm;
    auto receive = [&] {
        FramePtr frame(av_frame_alloc()); int r;
        while ((r = avcodec_receive_frame(dec.get(), frame.get())) >= 0) {
            if (measured) {
                Json names = Json::array();
                for (int ch = 0; ch < frame->ch_layout.nb_channels; ++ch) {
                    char name[16]; test(av_channel_name(name, sizeof(name), av_channel_layout_channel_from_index(&frame->ch_layout, ch)) >= 0); names.push_back(name);
                }
                Json now{{"sample_rate", frame->sample_rate}, {"channel_order", names}, {"bits_per_sample", dec->bits_per_raw_sample}, {"profile", dec->profile}};
                if (measured->empty()) {
                    *measured = now;
                    int64_t pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
                    (*measured)["first_pts"] = source_tb.num > 0 && pts != AV_NOPTS_VALUE ? Json(std::to_string(av_rescale_q(pts, source_tb, {1, frame->sample_rate}))) : Json(nullptr);
                } else for (auto& [key, value] : now.items()) test(measured->at(key) == value);
                (*measured)["decoded_frames"] = measured->value("decoded_frames", 0) + 1;
                if (source_tb.num > 0 && frame->pts != AV_NOPTS_VALUE && !measured->at("first_pts").is_null()) {
                    int64_t first = decimal<int64_t>(measured->at("first_pts"));
                    auto before = pcm.size() / frame->ch_layout.nb_channels; test(before <= static_cast<size_t>(INT64_MAX) && first <= INT64_MAX - static_cast<int64_t>(before));
                    int64_t expected = first + static_cast<int64_t>(before), observed = av_rescale_q(frame->pts, source_tb, {1, frame->sample_rate});
                    uint64_t difference = observed >= expected ? static_cast<uint64_t>(observed) - static_cast<uint64_t>(expected) : static_cast<uint64_t>(expected) - static_cast<uint64_t>(observed); test(difference < static_cast<uint64_t>(INT64_MAX));
                    int64_t delta = static_cast<int64_t>(difference), previous = decimal<int64_t>(measured->value("max_abs_delta_samples", Json("0")));
                    (*measured)["max_abs_delta_samples"] = std::to_string(std::max(delta, previous));
                    int64_t tolerance = std::max<int64_t>(2, av_rescale_q(1, source_tb, {1, frame->sample_rate}));
                    (*measured)["tolerance_samples"] = std::to_string(tolerance);
                    if (delta > tolerance && !measured->contains("first_discontinuous_frame")) (*measured)["first_discontinuous_frame"] = measured->at("decoded_frames").get<int>() - 1;
                }
            }
            if (trace && trace->size() < 32) trace->push_back({{"pts", frame->pts == AV_NOPTS_VALUE ? Json(nullptr) : Json(std::to_string(frame->pts))}, {"samples", frame->nb_samples}, {"rate", frame->sample_rate}, {"samples_before", std::to_string(pcm.size() / frame->ch_layout.nb_channels)}});
            auto values = canonical(frame.get()); test(values.size() <= 16777216 - pcm.size()); pcm.insert(pcm.end(), values.begin(), values.end()); av_frame_unref(frame.get());
        }
        test(r == AVERROR_EOF || r == AVERROR(EAGAIN));
    };
    for (const auto& p : packets) {
        PacketPtr packet(av_packet_alloc()); test(av_new_packet(packet.get(), p.payload.size()) >= 0);
        std::memcpy(packet->data, p.payload.data(), p.payload.size()); packet->pts = p.flags & 1 ? p.pts : AV_NOPTS_VALUE; packet->dts = p.flags & 2 ? p.dts : AV_NOPTS_VALUE;
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
    if (!export_dir.empty()) {
        Json fixture{{"start", start}, {"packets", Json::array()}};
        for (const auto& packet : input) fixture["packets"].push_back(base64(serialize(packet)));
        std::ofstream(export_dir + "/" + codec + (channels != 2 ? "-" + std::to_string(channels) + "ch" : variant != "base" ? "-" + variant : "") + ".json") << fixture.dump();
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
static void source_clock_regression() {
    // Derived timestamps only from the authorized Matroska probe; no media fixture.
    auto input = generate(AV_CODEC_ID_DTS, 2, 48000);
    const int ticks[] = {0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 116};
    Json start{{"type", "start"}, {"protocol", 1}, {"generation", "1"},
        {"input", {{"codec", "dts"}, {"time_base", {{"num", 1}, {"den", 1000}}}, {"extradata_base64", ""},
            {"sample_rate_hint", nullptr}, {"bits_per_sample_hint", nullptr}, {"channel_order_hint", nullptr}}},
        {"output", {{"codec", "flac"}}}, {"timeline", {{"audible_from_us", "0"}}}};
    CodecPipeline pipeline(start, 1, 65536, [](auto) {}, [](auto) {});
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto packet = input[i]; packet.pts = packet.dts = ticks[i]; packet.duration = 0;
        if (i + 1 == std::size(ticks)) rejects([&] { pipeline.submit(packet); }, "timeline_discontinuity");
        else pipeline.submit(packet);
    }
    std::cout << "source clock regression PASS: 64-sample discrepancy exceeds 48-sample tick; strict rejection preserved\n";
}
static void format_change_regression() {
    auto first = generate(AV_CODEC_ID_TRUEHD, 2, 48000), changed = generate(AV_CODEC_ID_TRUEHD, 2, 96000);
    Json start{{"type", "start"}, {"protocol", 1}, {"generation", "1"},
        {"input", {{"codec", "truehd"}, {"time_base", {{"num", 1}, {"den", 48000}}}, {"extradata_base64", ""},
            {"sample_rate_hint", nullptr}, {"bits_per_sample_hint", nullptr}, {"channel_order_hint", nullptr}}},
        {"output", {{"codec", "flac"}}}, {"timeline", {{"audible_from_us", "0"}}}};
    CodecPipeline pipeline(start, 1, 65536, [](auto) {}, [](auto) {});
    for (const auto& packet : first) pipeline.submit(packet);
    bool rejected = false;
    for (auto packet : changed) {
        packet.sequence += first.size();
        try { pipeline.submit(packet); } catch (const Failure& error) { test(std::string(error.what()) == "format_changed"); rejected = true; break; }
    }
    test(rejected); std::cout << "real midstream TrueHD rate change PASS: format_changed, no resampling/reconfiguration\n";
}
// Test-only packets stay in RAM. The public result contains derived properties/hashes.
static std::vector<uint8_t> packet_base64(const Json& value) {
    test(value.is_string()); const auto& text = value.get_ref<const std::string&>();
    test(text.size() <= ((1048640 + 2) / 3) * 4);
    std::vector<uint8_t> bytes;
    for (size_t offset = 0; offset < text.size(); offset += 65536) {
        auto part = unbase64(text.substr(offset, 65536)); bytes.insert(bytes.end(), part.begin(), part.end());
    }
    return bytes;
}
static std::string pcm_hash(const std::vector<int32_t>& samples) {
    std::vector<uint8_t> canonical_bytes(samples.size() * 4);
    for (size_t i = 0; i < samples.size(); ++i) AV_WL32(canonical_bytes.data() + i * 4, static_cast<uint32_t>(samples[i]));
    std::unique_ptr<AVSHA, decltype(&av_free)> sha(av_sha_alloc(), av_free); test(bool(sha));
    test(av_sha_init(sha.get(), 256) == 0); av_sha_update(sha.get(), canonical_bytes.data(), canonical_bytes.size());
    uint8_t digest[32]; av_sha_final(sha.get(), digest);
    const char* hex = "0123456789abcdef"; std::string out;
    for (auto byte : digest) { out += hex[byte >> 4]; out += hex[byte & 15]; }
    return out;
}
static Json validate_remote(const Json& value) {
    const auto& fixture = value.at("fixture"); const auto& observation = value.at("observation");
    const auto& start = fixture.at("start"); const auto& format = observation.at("format");
    const auto& end = observation.at("end"); auto generation = decimal<uint64_t>(start.at("generation"));
    test(format.at("type") == "output_format" && format.at("codec") == "flac" && decimal<uint64_t>(format.at("generation")) == generation);
    std::string name = start.at("input").at("codec");
    AVCodecID id = name == "dts" ? AV_CODEC_ID_DTS : name == "truehd" ? AV_CODEC_ID_TRUEHD : name == "mlp" ? AV_CODEC_ID_MLP : AV_CODEC_ID_NONE;
    test(id != AV_CODEC_ID_NONE);
    AVRational tb{integer(start.at("input").at("time_base").at("num"), 1, 1000000000), integer(start.at("input").at("time_base").at("den"), 1, 1000000000)};
    std::vector<Packet> input, output;
    for (const auto& encoded : fixture.at("packets")) {
        test(input.size() < 65536); input.push_back(parse_packet(packet_base64(encoded), 1, generation, input.size()));
    }
    int64_t next = decimal<int64_t>(end.at("first_pts")); uint64_t total = 0;
    for (const auto& encoded : observation.at("packets")) {
        test(output.size() < 65536); auto packet = parse_packet(packet_base64(encoded), 2, generation, output.size());
        test(packet.pts == next && next <= INT64_MAX - packet.duration);
        next += packet.duration; total = add(total, packet.duration); output.push_back(std::move(packet));
    }
    test(end.at("type") == "end_output" && decimal<uint64_t>(end.at("generation")) == generation);
    test(decimal<uint64_t>(end.at("input_packets")) == input.size() && decimal<uint64_t>(end.at("output_packets")) == output.size());
    test(total == decimal<uint64_t>(end.at("total_samples")) && next == decimal<int64_t>(end.at("end_pts_exclusive")));
    Json source_format, actual_format;
    auto reference = decode(id, input, start.at("input"), &source_format, tb);
    auto actual = decode(AV_CODEC_ID_FLAC, output, format, &actual_format);
    test(!reference.empty() && reference == actual);
    if (fixture.value("expected_source_profile", "") == "dts-hd-ma") {
        int profile = source_format.at("profile");
        test(profile == AV_PROFILE_DTS_HD_MA || profile == AV_PROFILE_DTS_HD_MA_X || profile == AV_PROFILE_DTS_HD_MA_X_IMAX);
        test(format.at("source_profile") == "dts-hd-ma");
    }
    test(source_format.at("sample_rate") == actual_format.at("sample_rate") && source_format.at("channel_order") == actual_format.at("channel_order"));
    test(source_format.at("bits_per_sample") <= actual_format.at("bits_per_sample"));
    for (const char* key : {"sample_rate", "channel_order", "bits_per_sample"}) test(format.at(key) == actual_format.at(key));
    test(format.at("time_base").at("num") == 1 && format.at("time_base").at("den") == actual_format.at("sample_rate"));
    test(source_format.at("first_pts") == end.at("first_pts"));
    auto channels = actual_format.at("channel_order").size(); test(total * channels == actual.size());
    return {{"status", "passed"}, {"criterion_id", "external_audio.host_pcm_equivalence.v1"},
        {"codec", name}, {"sample_rate", format.at("sample_rate")}, {"channel_order", format.at("channel_order")},
        {"bits_per_sample", format.at("bits_per_sample")}, {"valid_samples", std::to_string(total)},
        {"first_pts", end.at("first_pts")}, {"end_pts_exclusive", end.at("end_pts_exclusive")},
        {"reference_pcm_sha256", pcm_hash(reference)}, {"output_pcm_sha256", pcm_hash(actual)}, {"physical_acceptance", false}};
}
static Json inspect_source(const Json& fixture) {
    const auto& input = fixture.at("start").at("input"); std::string name = input.at("codec");
    AVCodecID id = name == "dts" ? AV_CODEC_ID_DTS : name == "truehd" ? AV_CODEC_ID_TRUEHD : name == "mlp" ? AV_CODEC_ID_MLP : AV_CODEC_ID_NONE; test(id != AV_CODEC_ID_NONE);
    auto generation = decimal<uint64_t>(fixture.at("start").at("generation")); std::vector<Packet> packets;
    for (const auto& p : fixture.at("packets")) packets.push_back(parse_packet(packet_base64(p), 1, generation, packets.size()));
    Json measured, trace = Json::array(); AVRational tb{integer(input.at("time_base").at("num"), 1, 1000000000), integer(input.at("time_base").at("den"), 1, 1000000000)};
    auto pcm = decode(id, packets, input, &measured, tb, &trace);
    Json groups = Json::array(); bool groups_truncated = false;
    if (fixture.contains("demux_positions")) {
        test(fixture.at("demux_positions").size() == packets.size());
        for (size_t i = 0; i < packets.size(); ++i) {
            int64_t position = decimal<int64_t>(fixture.at("demux_positions")[i]);
            if (groups.empty() || position < 0 || groups.back().at("position_bytes") != std::to_string(position)) {
                if (groups.size() >= 64) { groups_truncated = true; break; }
                groups.push_back({{"position_bytes", std::to_string(position)}, {"first_sequence", std::to_string(i)}, {"input_packets", 0}, {"first_pts", std::to_string(packets[i].pts)}, {"last_pts", std::to_string(packets[i].pts)}});
            }
            groups.back()["input_packets"] = groups.back().at("input_packets").get<int>() + 1;
            groups.back()["last_pts"] = std::to_string(packets[i].pts);
        }
    }
    int profile = measured.at("profile"); measured.erase("profile");
    measured["source_profile"] = profile == AV_PROFILE_DTS_HD_MA || profile == AV_PROFILE_DTS_HD_MA_X || profile == AV_PROFILE_DTS_HD_MA_X_IMAX ? "dts-hd-ma" : name;
    return {{"status", "passed"}, {"criterion_id", "external_audio.host_source_timeline.v1"},
        {"format", measured}, {"trace", trace}, {"time_base", input.at("time_base")},
        {"demux_position_groups", groups}, {"demux_groups_truncated", groups_truncated},
        {"trace_truncated", measured.at("decoded_frames").get<size_t>() > trace.size()},
        {"source_timeline_accepted", !measured.contains("first_discontinuous_frame")},
        {"valid_samples", std::to_string(pcm.size() / measured.at("channel_order").size())}, {"canonical_pcm_sha256", pcm_hash(pcm)}};
}
static Json inspect_after_recovery(const Json& fixture) {
    const auto& input = fixture.at("start").at("input"); test(input.at("codec") == "dts");
    auto generation = decimal<uint64_t>(fixture.at("start").at("generation"));
    AVRational tb{integer(input.at("time_base").at("num"), 1, 1000000000), integer(input.at("time_base").at("den"), 1, 1000000000)};
    CodecPtr decoder(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_DTS))); test(bool(decoder));
    decoder->thread_count = 1; decoder->flags |= AV_CODEC_FLAG_BITEXACT; decoder->pkt_timebase = tb;
    decoder->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
    auto extra = unbase64(input.at("extradata_base64"));
    if (!extra.empty()) { decoder->extradata = static_cast<uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE)); test(decoder->extradata); std::memcpy(decoder->extradata, extra.data(), extra.size()); decoder->extradata_size = extra.size(); }
    test(avcodec_open2(decoder.get(), decoder->codec, nullptr) >= 0);
    Json measured; size_t sequence = 0; int rejected = 0, frames = 0; std::optional<int64_t> first;
    for (const auto& encoded : fixture.at("packets")) {
        test(sequence < 256); auto packet = parse_packet(packet_base64(encoded), 1, generation, sequence++);
        if (packet.flags & 1) {
            if (!first) first = packet.pts;
            if (packet.pts >= *first) {
                uint64_t elapsed = static_cast<uint64_t>(packet.pts) - static_cast<uint64_t>(*first);
                uint64_t deadline = (2ULL * tb.den + tb.num - 1) / tb.num; test(elapsed < deadline);
            }
        }
        PacketPtr native(av_packet_alloc()); test(bool(native)); test(av_new_packet(native.get(), packet.payload.size()) >= 0);
        std::memcpy(native->data, packet.payload.data(), packet.payload.size()); native->pts = packet.flags & 1 ? packet.pts : AV_NOPTS_VALUE; native->dts = packet.flags & 2 ? packet.dts : AV_NOPTS_VALUE;
        auto before = observed_xll_recovery.load(); int result = avcodec_send_packet(decoder.get(), native.get());
        if (result < 0) {
            // Inspect subsequent metadata only after this exact known recovery
            // marker. This is NOT a playback/trim/fallback policy or a PCM pass.
            test(frames == 0 && rejected == 0 && observed_xll_recovery.load() > before);
            ++rejected; continue;
        }
        for (;;) {
            FramePtr frame(av_frame_alloc()); test(bool(frame)); result = avcodec_receive_frame(decoder.get(), frame.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            test(result >= 0 && !(frame->flags & AV_FRAME_FLAG_CORRUPT) && !frame->decode_error_flags);
            Json channels = Json::array();
            for (int ch = 0; ch < frame->ch_layout.nb_channels; ++ch) { char name[16]; test(av_channel_name(name, sizeof(name), av_channel_layout_channel_from_index(&frame->ch_layout, ch)) >= 0); channels.push_back(name); }
            Json now{{"sample_rate", frame->sample_rate}, {"bits_per_sample", decoder->bits_per_raw_sample}, {"channel_order", channels}};
            test(decoder->profile == AV_PROFILE_DTS_HD_MA || decoder->profile == AV_PROFILE_DTS_HD_MA_X || decoder->profile == AV_PROFILE_DTS_HD_MA_X_IMAX);
            if (measured.empty()) measured = now; else test(measured == now);
            ++frames;
            if (frames == 8) return {{"status", "diagnostic_only"}, {"criterion_id", "external_audio.decoder_policy.v1"},
                {"xll_recovery_packets_rejected", rejected}, {"full_hd_frames_inspected", frames}, {"format", measured},
                {"leading_coverage_missing", rejected > 0}, {"source_acceptance", false}, {"physical_acceptance", false}};
        }
    }
    throw std::runtime_error("no bounded full-HD metadata observation");
}
static int remote_validation_stdin(bool inspect = false, bool recovery_probe = false) {
    try {
        std::string text; char block[8192];
        while (std::cin.read(block, sizeof(block)) || std::cin.gcount()) {
            test(text.size() + std::cin.gcount() <= 64 * 1024 * 1024); text.append(block, std::cin.gcount());
        }
        std::cout << (recovery_probe ? inspect_after_recovery(Json::parse(text)) : inspect ? inspect_source(Json::parse(text)) : validate_remote(Json::parse(text))).dump() << '\n'; return 0;
    } catch (const std::exception& error) {
        Json failure{{"status", "failed"}, {"code", "output_validation_failed"},
            {"decoder_observation", {{"criterion_id", "external_audio.decoder_policy.v1"}, {"xll_recovery_requested", std::to_string(observed_xll_recovery.load())}}}};
        std::string text = error.what(), prefix = "test assertion failed at line ";
        if (text.starts_with(prefix) && text.size() < 64 && std::all_of(text.begin() + prefix.size(), text.end(), [](char c) {return c >= '0' && c <= '9';})) failure["check_line"] = std::stoi(text.substr(prefix.size()));
        std::cout << failure.dump() << '\n'; return 1;
    }
}
int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_QUIET);
    if (argc == 2 && std::string(argv[1]) == "--inspect-after-recovery") { av_log_set_level(AV_LOG_VERBOSE); av_log_set_callback(policy_log); return remote_validation_stdin(false, true); }
    if (argc == 2 && std::string(argv[1]) == "--inspect-source") { av_log_set_level(AV_LOG_VERBOSE); av_log_set_callback(policy_log); return remote_validation_stdin(true); }
    if (argc == 2 && std::string(argv[1]) == "--validate-remote") return remote_validation_stdin();
    try {
        test(argc == 2 || argc == 3); if (argc == 3) { export_dir = argv[2]; std::filesystem::create_directories(export_dir); } pure(argv[1]); CodecPipeline::selfcheck();
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000);
        roundtrip(AV_CODEC_ID_MLP, "mlp", 2, 96000);
        roundtrip(AV_CODEC_ID_DTS, "dts", 2, 48000);
        roundtrip(AV_CODEC_ID_DTS, "dts", 6, 48000);
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000, "negative");
        roundtrip(AV_CODEC_ID_TRUEHD, "truehd", 2, 48000, "missing-after-anchor");
        roundtrip(AV_CODEC_ID_DTS, "dts", 2, 48000, "skip-samples");
        roundtrip(AV_CODEC_ID_MLP, "mlp", 6, 96000);
        source_clock_regression(); format_change_regression();
        std::cout << "core checks PASS (DTS-HD MA hardware fixture remains required)\n";
    } catch (const std::exception& e) { std::cerr << "core checks FAILED: " << e.what() << '\n'; return 1; }
}
