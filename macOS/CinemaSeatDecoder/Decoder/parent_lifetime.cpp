#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <unistd.h>

namespace {
// The menu bar app holds the write end. EOF also arrives if it is force quit.
struct ParentLifetime {
    ParentLifetime() {
        if (std::getenv("CINEMASEAT_DECODER_PARENT_PIPE") == nullptr) return;
        std::thread([] {
            char byte;
            ssize_t count;
            do {
                count = read(STDIN_FILENO, &byte, sizeof(byte));
            } while (count > 0 || (count < 0 && errno == EINTR));
            kill(getpid(), SIGTERM);
        }).detach();
    }
} parent_lifetime;
}
