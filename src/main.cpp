#include "server.hpp"
#include "codec_pipeline.hpp"
#include <iostream>
extern "C" {
#include <libavutil/log.h>
}
int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_QUIET);
    std::string path = "/config/settings.json"; bool health = false, registry = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--settings" && i + 1 < argc) path = argv[++i];
        else if (arg == "--healthcheck") health = true;
        else if (arg == "--codec-registry") registry = true;
        else { std::cerr << "fatal: invalid_arguments\n"; return 2; }
    }
    try {
        if (registry) {
            void* opaque = nullptr; const AVCodec* codec;
            while ((codec = av_codec_iterate(&opaque))) std::cout << (av_codec_is_encoder(codec) ? "encoder " : "decoder ") << codec->name << '\n';
            return 0;
        }
        auto config = csad::Config::read(path);
        if (health) return csad::healthcheck(config);
        return csad::serve(config);
    } catch (const csad::Failure& error) { std::cerr << "fatal: " << error.what() << '\n'; return 2;
    } catch (const std::exception&) { std::cerr << "fatal: invalid_configuration_or_startup\n"; return 2; }
}
