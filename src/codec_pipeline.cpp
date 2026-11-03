#include "codec_pipeline.hpp"
#include <cstring>
#include <array>
extern "C" {
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libavutil/sha.h>
}
namespace csad {
static void ok(int result, const char* code) { require(result >= 0, code); }
void CodecPipeline::selfcheck() {
    for (auto id : {AV_CODEC_ID_DTS, AV_CODEC_ID_TRUEHD, AV_CODEC_ID_MLP}) require(avcodec_find_decoder(id), "internal_error");
    require(avcodec_find_encoder(AV_CODEC_ID_FLAC), "internal_error");
}
CodecPipeline::CodecPipeline(const Json& s, int threads, int cap,
    std::function<void(const Json&)> f, std::function<void(Packet)> p, std::function<bool()> c)
    : start(s), generation(decimal<uint64_t>(s.at("generation"))), fifo_cap(cap),
      on_format(std::move(f)), on_packet(std::move(p)), cancelled(std::move(c)) {
    require(s.at("type") == "start" && integer(s.at("protocol"), 1, 1) == 1);
    require(generation > 0 && generation <= INT64_MAX);
    require(s.at("output").at("codec") == "flac", "unsupported_codec");
    (void)decimal<int64_t>(s.at("timeline").at("audible_from_us"));
    const auto& input = s.at("input");
    const std::string name = input.at("codec").get<std::string>();
    if (s.at("output").contains("layout_policy")) {
        require(input.contains("channel_order_hint"));
        const auto& policy = s.at("output").at("layout_policy");
        compatible_dts = policy == "dts-native-or-side-7.1-v1";
        require(compatible_dts || policy == "dts-side-5.1-to-7.1-silence-v1", "unsupported_format");
        require(name == "dts", "unsupported_format");
        if (!compatible_dts) require(input.at("channel_order_hint").is_null() || input.at("channel_order_hint") == Json({"FL","FR","FC","LFE","SL","SR"}), "unsupported_format");
        pad_side = !compatible_dts;
    }
    AVCodecID id = name == "dts" ? AV_CODEC_ID_DTS : name == "truehd" ? AV_CODEC_ID_TRUEHD : name == "mlp" ? AV_CODEC_ID_MLP : AV_CODEC_ID_NONE;
    require(id != AV_CODEC_ID_NONE, "unsupported_codec");
    for (const char* key : {"sample_rate_hint", "bits_per_sample_hint", "channel_order_hint"}) require(input.contains(key));
    if (!input.at("sample_rate_hint").is_null()) integer(input.at("sample_rate_hint"), 8000, 192000);
    if (!input.at("bits_per_sample_hint").is_null()) integer(input.at("bits_per_sample_hint"), 1, 24);
    if (!input.at("channel_order_hint").is_null()) require(input.at("channel_order_hint").is_array() && !input.at("channel_order_hint").empty() && input.at("channel_order_hint").size() <= 8);
    input_tb = {integer(input.at("time_base").at("num"), 1, 1000000000), integer(input.at("time_base").at("den"), 1, 1000000000)};
    decoder.reset(avcodec_alloc_context3(avcodec_find_decoder(id))); require(bool(decoder), "internal_error");
    decoder->pkt_timebase = input_tb; decoder->thread_count = threads;
    decoder->flags |= AV_CODEC_FLAG_BITEXACT;
    decoder->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
    auto extra = unbase64(input.at("extradata_base64"));
    if (!extra.empty()) {
        decoder->extradata = static_cast<uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        require(decoder->extradata, "internal_error");
        std::memcpy(decoder->extradata, extra.data(), extra.size()); decoder->extradata_size = extra.size();
    }
    ok(avcodec_open2(decoder.get(), decoder->codec, nullptr), "decode_failed");
}
void CodecPipeline::check_cancel() const { require(!cancelled(), "cancelled"); }
void CodecPipeline::submit(const Packet& p) {
    check_cancel(); require(!finished && p.generation == generation && p.sequence == input_count, "invalid_packet");
    require(!p.payload.empty() && p.payload.size() <= 1048576, "invalid_packet");
    require((p.flags & ~7) == 0 && p.duration >= 0, "invalid_packet");
    submitting=&p;
    struct Reset {const Packet*& value; ~Reset(){value=nullptr;}} reset{submitting};
    PacketPtr packet(av_packet_alloc()); require(bool(packet), "internal_error");
    ok(av_new_packet(packet.get(), p.payload.size()), "internal_error");
    std::memcpy(packet->data, p.payload.data(), p.payload.size());
    packet->pts = p.flags & 1 ? p.pts : AV_NOPTS_VALUE; packet->dts = p.flags & 2 ? p.dts : AV_NOPTS_VALUE;
    packet->duration = p.duration; packet->time_base = input_tb;
    if (p.skip_start || p.skip_end) {
        auto* side = av_packet_new_side_data(packet.get(), AV_PKT_DATA_SKIP_SAMPLES, 10);
        require(side, "internal_error"); AV_WL32(side, p.skip_start); AV_WL32(side + 4, p.skip_end); side[8] = side[9] = 0;
    }
    int result = avcodec_send_packet(decoder.get(), packet.get());
    if (result == AVERROR(EAGAIN)) { decode_frames(); result = avcodec_send_packet(decoder.get(), packet.get()); }
    ok(result, "decode_failed"); input_count = add(input_count, 1); decode_frames();
    if (!anchored && (p.flags & 1)) {
        if (!priming_pts) priming_pts = p.pts;
        long double seconds = (static_cast<long double>(p.pts) - *priming_pts) * input_tb.num / input_tb.den;
        require(seconds < 2, "decode_failed");
    }
    // Conservative priming: no corrupt packet is silently skipped.
    require(anchored || input_count < 256, "decode_failed");
}
void CodecPipeline::decode_frames() {
    FramePtr frame(av_frame_alloc()); require(bool(frame), "internal_error");
    for (;;) {
        check_cancel(); int result = avcodec_receive_frame(decoder.get(), frame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
        ok(result, "decode_failed");
        require(!(frame->flags & AV_FRAME_FLAG_CORRUPT) && !frame->decode_error_flags, "decode_failed");
        decoded(frame.get()); av_frame_unref(frame.get());
    }
}
static Json channel_names(const AVChannelLayout& layout) {
    require(layout.order == AV_CHANNEL_ORDER_NATIVE && av_channel_layout_check(&layout), "unsupported_format");
    // Preserve FFmpeg-supported native FLAC order, including side/back layouts.
    // STREAMINFO carries channel count only: clients MUST retain channel_order.
    static const uint64_t masks[] = {AV_CH_LAYOUT_MONO, AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_SURROUND,
        AV_CH_LAYOUT_QUAD, AV_CH_LAYOUT_5POINT0_BACK, AV_CH_LAYOUT_5POINT1_BACK, AV_CH_LAYOUT_6POINT1, AV_CH_LAYOUT_7POINT1};
    bool native_flac = layout.nb_channels >= 1 && layout.nb_channels <= 8 && layout.u.mask == masks[layout.nb_channels - 1];
    bool explicit_side = (layout.nb_channels == 4 && layout.u.mask == AV_CH_LAYOUT_2_2) ||
        (layout.nb_channels == 5 && layout.u.mask == AV_CH_LAYOUT_5POINT0) ||
        (layout.nb_channels == 6 && layout.u.mask == AV_CH_LAYOUT_5POINT1);
    require(native_flac || explicit_side, "unsupported_format");
    Json names = Json::array();
    for (int i = 0; i < layout.nb_channels; ++i) {
        char name[16]; ok(av_channel_name(name, sizeof(name), av_channel_layout_channel_from_index(&layout, i)), "unsupported_format"); names.push_back(name);
    }
    return names;
}
void CodecPipeline::open_encoder(AVFrame* frame) {
    auto channels = channel_names(frame->ch_layout);
    source_mask = frame->ch_layout.u.mask;
    if (compatible_dts) pad_side = source_mask == AV_CH_LAYOUT_5POINT1 && frame->ch_layout.nb_channels == 6;
    if (pad_side) require(source_mask == AV_CH_LAYOUT_5POINT1 && frame->ch_layout.nb_channels == 6, "unsupported_format");
    require(frame->sample_rate >= 8000 && frame->sample_rate <= 192000, "unsupported_format");
    auto packed = av_get_packed_sample_fmt(static_cast<AVSampleFormat>(frame->format));
    require(packed == AV_SAMPLE_FMT_S16 || packed == AV_SAMPLE_FMT_S32, "unsupported_format");
    int precision = decoder->bits_per_raw_sample;
    require(precision >= 1 && precision <= 24, "unsupported_format");
    const auto& hint = start.at("input");
    if (!hint.at("sample_rate_hint").is_null()) require(hint.at("sample_rate_hint") == frame->sample_rate, "unsupported_format");
    if (!hint.at("bits_per_sample_hint").is_null()) require(hint.at("bits_per_sample_hint") == precision, "unsupported_format");
    if (!hint.at("channel_order_hint").is_null()) require(hint.at("channel_order_hint") == channels, "unsupported_format");
    encoder.reset(avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_FLAC))); require(bool(encoder), "internal_error");
    encoder->sample_fmt = packed; encoder->sample_rate = frame->sample_rate;
    encoder->bits_per_raw_sample = packed == AV_SAMPLE_FMT_S16 ? 16 : 24;
    encoder->time_base = {1, frame->sample_rate}; encoder->thread_count = decoder->thread_count;
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; encoder->compression_level = 1; encoder->frame_size = 4096;
    if (pad_side) ok(av_channel_layout_from_mask(&encoder->ch_layout, AV_CH_LAYOUT_7POINT1), "internal_error");
    else ok(av_channel_layout_copy(&encoder->ch_layout, &frame->ch_layout), "internal_error");
    ok(avcodec_open2(encoder.get(), encoder->codec, nullptr), "encode_failed");
    require(encoder->frame_size == 4096 && encoder->extradata_size == 34 && encoder->sample_rate == frame->sample_rate && encoder->sample_fmt == packed, "encode_failed");
    uint64_t stream = AV_RB64(encoder->extradata + 10);
    require(int(stream >> 44) == encoder->sample_rate && int(((stream >> 41) & 7) + 1) == encoder->ch_layout.nb_channels && int(((stream >> 36) & 31) + 1) == encoder->bits_per_raw_sample, "encode_failed");
    fifo.reset(av_audio_fifo_alloc(packed, encoder->ch_layout.nb_channels, fifo_cap)); require(bool(fifo), "internal_error");
    input_format = frame->format; input_precision = precision;
    const char* profile = decoder->codec_id == AV_CODEC_ID_DTS ? "dts" : decoder->codec_id == AV_CODEC_ID_TRUEHD ? "truehd" : "mlp";
    if (decoder->profile == AV_PROFILE_DTS_HD_MA || decoder->profile == AV_PROFILE_DTS_HD_MA_X || decoder->profile == AV_PROFILE_DTS_HD_MA_X_IMAX) profile = "dts-hd-ma";
    format = {{"type", "output_format"}, {"generation", std::to_string(generation)}, {"codec", "flac"},
        {"sample_rate", encoder->sample_rate}, {"bits_per_sample", encoder->bits_per_raw_sample}, {"channel_order", channel_names(encoder->ch_layout)},
        {"time_base", {{"num", 1}, {"den", encoder->sample_rate}}}, {"extradata_base64", base64({encoder->extradata, 34})},
        {"source_profile", profile}, {"object_metadata_preserved", false}};
    if (pad_side || compatible_dts) {
        format["layout_policy"] = compatible_dts ? "dts-native-or-side-7.1-v1" : "dts-side-5.1-to-7.1-silence-v1";
        format["source_channel_order"] = channels;
        format["silent_channels"] = pad_side ? Json({"BL","BR"}) : Json::array();
    }
    on_format(format);
}
Json CodecPipeline::timeline_failure(AVFrame* frame, int64_t timestamp, int64_t observed, int64_t tolerance) const {
    Json input=nullptr;
    if(submitting) {
        const auto& p=*submitting;
        std::array<uint8_t,32> digest{}; auto* hash=av_sha_alloc(); require(hash,"internal_error");
        av_sha_init(hash,256); av_sha_update(hash,p.payload.data(),p.payload.size()); av_sha_final(hash,digest.data()); av_free(hash);
        static const char hex[]="0123456789abcdef"; std::string encoded;encoded.reserve(64);
        for(auto byte:digest){encoded+=hex[byte>>4];encoded+=hex[byte&15];}
        input={{"sequence",std::to_string(p.sequence)},{"flags",p.flags},{"pts",p.flags&1?Json(std::to_string(p.pts)):Json(nullptr)},
            {"dts",p.flags&2?Json(std::to_string(p.dts)):Json(nullptr)},{"duration",std::to_string(p.duration)},
            {"skip_start",p.skip_start},{"skip_end",p.skip_end},{"payload_bytes",p.payload.size()},{"payload_sha256",encoded}};
    }
    return {{"contract","external_audio.timeline_failure.v1"},{"expected_pts",std::to_string(next_pts)},
        {"first_pts",std::to_string(first_pts)},{"observed_pts",std::to_string(observed)},{"tolerance_samples",std::to_string(tolerance)},
        {"accepted_samples",std::to_string(samples)},{"decoded_frame_index",std::to_string(decoded_frames-1)},
        {"input_packets",std::to_string(input_count)},{"sample_rate",encoder->sample_rate},
        {"time_base_num",input_tb.num},{"time_base_den",input_tb.den},{"frame_samples",frame->nb_samples},
        {"frame_timestamp",std::to_string(timestamp)},{"timestamp_kind",frame->pts!=AV_NOPTS_VALUE?"pts":"best_effort"},
        {"packet_binding",submitting?"most-recent-submit":"flush-without-packet"},{"input",input}};
}
void CodecPipeline::decoded(AVFrame* frame) {
    decoded_frames=add(decoded_frames,1);
    require(frame->nb_samples > 0 && frame->nb_samples <= fifo_cap, "limit_exceeded");
    if (!encoder) open_encoder(frame);
    require(frame->sample_rate == encoder->sample_rate && frame->format == input_format && decoder->bits_per_raw_sample == input_precision &&
        (pad_side ? av_channel_layout_check(&frame->ch_layout) && frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE && frame->ch_layout.u.mask == source_mask : av_channel_layout_compare(&frame->ch_layout, &encoder->ch_layout) == 0), "format_changed");
    int64_t timestamp = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    if (!anchored) {
        require(timestamp != AV_NOPTS_VALUE, "missing_timeline");
        first_pts = next_pts = encoded_pts = av_rescale_q(timestamp, input_tb, encoder->time_base);
        require(first_pts != INT64_MIN && first_pts != INT64_MAX, "missing_timeline"); anchored = true;
    } else if (timestamp != AV_NOPTS_VALUE) {
        int64_t observed = av_rescale_q(timestamp, input_tb, encoder->time_base);
        int64_t tolerance = std::max<int64_t>(2, av_rescale_q(1, input_tb, encoder->time_base));
        // Use wide differences without signed overflow near timestamp limits.
        if(std::abs(static_cast<long double>(observed) - next_pts)>tolerance)
            throw Failure("timeline_discontinuity",timeline_failure(frame,timestamp,observed,tolerance));
    }
    require(next_pts <= INT64_MAX - frame->nb_samples, "limit_exceeded");
    FramePtr packed(av_frame_alloc()); require(bool(packed), "internal_error");
    packed->format = encoder->sample_fmt; packed->sample_rate = encoder->sample_rate; packed->nb_samples = frame->nb_samples;
    ok(av_channel_layout_copy(&packed->ch_layout, &encoder->ch_layout), "internal_error");
    ok(av_frame_get_buffer(packed.get(), 0), "internal_error");
    int bytes = av_get_bytes_per_sample(encoder->sample_fmt), channels = encoder->ch_layout.nb_channels;
    bool planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format));
    for (int i = 0; i < frame->nb_samples; ++i) for (int ch = 0; ch < channels; ++ch) {
        auto* dst = packed->data[0] + (i * channels + ch) * bytes;
        if (pad_side && (ch == 4 || ch == 5)) { std::memset(dst, 0, bytes); continue; }
        // Native order: FL FR FC LFE [silent BL BR] SL SR.
        int source_ch = pad_side && ch >= 6 ? ch - 2 : ch;
        int source_channels = frame->ch_layout.nb_channels;
        auto* src = frame->extended_data[planar ? source_ch : 0] + (planar ? i : i * source_channels + source_ch) * bytes;
        if (bytes == 4) { int32_t sample; std::memcpy(&sample, src, 4); require((sample & 255) == 0, "unsupported_format"); }
        std::memcpy(dst, src, bytes);
    }
    require(frame->nb_samples <= fifo_cap - av_audio_fifo_size(fifo.get()), "limit_exceeded");
    require(av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(packed->data), frame->nb_samples) == frame->nb_samples, "internal_error");
    next_pts += frame->nb_samples; samples = add(samples, frame->nb_samples); require(samples <= INT64_MAX, "limit_exceeded"); encode_fifo(false);
}
void CodecPipeline::encode_fifo(bool tail) {
    while (av_audio_fifo_size(fifo.get()) >= encoder->frame_size || (tail && av_audio_fifo_size(fifo.get()) > 0)) {
        check_cancel(); FramePtr frame(av_frame_alloc()); require(bool(frame), "internal_error");
        frame->format = encoder->sample_fmt; frame->sample_rate = encoder->sample_rate;
        frame->nb_samples = std::min(av_audio_fifo_size(fifo.get()), encoder->frame_size); frame->pts = encoded_pts;
        ok(av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout), "internal_error"); ok(av_frame_get_buffer(frame.get(), 0), "internal_error");
        require(av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(frame->data), frame->nb_samples) == frame->nb_samples, "internal_error");
        int result = avcodec_send_frame(encoder.get(), frame.get());
        if (result == AVERROR(EAGAIN)) { encoded_packets(); result = avcodec_send_frame(encoder.get(), frame.get()); }
        ok(result, "encode_failed"); encoded_pts += frame->nb_samples; encoded_packets();
    }
}
void CodecPipeline::encoded_packets() {
    PacketPtr packet(av_packet_alloc()); require(bool(packet), "internal_error");
    for (;;) {
        check_cancel(); int result = avcodec_receive_packet(encoder.get(), packet.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
        ok(result, "encode_failed");
        // FFmpeg may emit a final zero-size extradata update: it is not audio.
        if (packet->size > 0) {
            require(packet->duration > 0 && packet->pts != AV_NOPTS_VALUE, "encode_failed");
            Packet p; p.kind = 2; p.flags = 3; p.generation = generation; p.sequence = output_count; output_count = add(output_count, 1);
            require(packet->pts == first_pts + static_cast<int64_t>(emitted_samples) && static_cast<uint64_t>(packet->duration) <= samples - emitted_samples, "encode_failed");
            emitted_samples = add(emitted_samples, packet->duration);
            p.pts = p.dts = packet->pts; p.duration = packet->duration;
            p.payload.assign(packet->data, packet->data + packet->size); on_packet(std::move(p));
        }
        av_packet_unref(packet.get());
    }
}
Json CodecPipeline::finish() {
    check_cancel(); require(!finished); finished = true;
    int result = avcodec_send_packet(decoder.get(), nullptr);
    if (result == AVERROR(EAGAIN)) { decode_frames(); result = avcodec_send_packet(decoder.get(), nullptr); }
    ok(result, "decode_failed"); decode_frames(); require(anchored, "missing_timeline");
    encode_fifo(true); ok(avcodec_send_frame(encoder.get(), nullptr), "encode_failed"); encoded_packets();
    require(emitted_samples == samples, "encode_failed");
    return {{"type", "end_output"}, {"generation", std::to_string(generation)},
        {"input_packets", std::to_string(input_count)}, {"output_packets", std::to_string(output_count)},
        {"total_samples", std::to_string(samples)}, {"first_pts", std::to_string(first_pts)}, {"end_pts_exclusive", std::to_string(next_pts)}};
}
}
