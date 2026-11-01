#pragma once
#include "wire.hpp"
#include <functional>
#include <memory>
#include <optional>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
}
namespace csad {
struct CodecFree { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameFree { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PacketFree { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FifoFree { void operator()(AVAudioFifo* p) const { av_audio_fifo_free(p); } };
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFree>;
using FramePtr = std::unique_ptr<AVFrame, FrameFree>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFree>;
class CodecPipeline {
    CodecPtr decoder, encoder;
    std::unique_ptr<AVAudioFifo, FifoFree> fifo;
    Json start, format;
    AVRational input_tb{};
    uint64_t generation, input_count = 0, output_count = 0, samples = 0, emitted_samples = 0;
    int64_t first_pts = 0, next_pts = 0, encoded_pts = 0;
    std::optional<int64_t> priming_pts;
    int input_format = -1, fifo_cap;
    bool anchored = false, finished = false;
    std::function<void(const Json&)> on_format;
    std::function<void(Packet)> on_packet;
    std::function<bool()> cancelled;
    void check_cancel() const;
    void decode_frames();
    void decoded(AVFrame* frame);
    void open_encoder(AVFrame* frame);
    void encode_fifo(bool tail);
    void encoded_packets();
public:
    CodecPipeline(const Json& start, int codec_threads, int fifo_samples,
                  std::function<void(const Json&)> on_format,
                  std::function<void(Packet)> on_packet,
                  std::function<bool()> cancelled = [] { return false; });
    void submit(const Packet& packet);
    Json finish();
    static void selfcheck();
};
}
