#include "server.hpp"
#include "bounded_queue.hpp"
#include "codec_pipeline.hpp"
#include "logging.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace csad {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;
using Clock = std::chrono::steady_clock;
static constexpr const char* revision = "946fcce07b6dcd0331c8cc609192aeff5e1924f8";
struct Server;
struct Message { bool text; std::vector<uint8_t> bytes; bool terminal = false; bool activates = false; };
struct Work { std::optional<Packet> packet; };

struct Session : std::enable_shared_from_this<Session> {
    Server& owner;
    const Config& config;
    ws::stream<beast::tcp_stream> socket;
    beast::flat_buffer read_buffer;
    http::request_parser<http::empty_body> request;
    net::steady_timer timer;
    net::steady_timer error_flush_timer;
    BoundedQueue<Work> inputs;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> working{false}, formatted{false}, waiting_output{false};
    std::atomic<int64_t> progress_ns{0};
    std::atomic<uint64_t> input_owned{0}, input_peak{0};
    size_t output_peak = 0;
    std::mutex mutex;
    std::condition_variable space;
    std::deque<Message> controls, outputs;
    std::optional<Message> pending_credit;
    std::shared_ptr<Message> writing;
    size_t output_bytes = 0;
    uint64_t generation = 0, received = 0, received_bytes = 0, consumed_bytes = 0;
    uint64_t input_limit;
    Credit output_credit;
    bool slot = false, upgraded = false, started = false, activated = false, eof = false, closing = false, close_started = false;
    bool pinging = false, awaiting_pong = false, flush_error = false;
    ws::close_code close_code = ws::close_code::normal;
    Clock::time_point opened = Clock::now(), ping_at = opened, pong_due{}, stage_started = opened, write_started = opened;
    std::string id = boost::uuids::to_string(boost::uuids::random_generator()());
    Session(Server& owner, tcp::socket s, const Config& config);
    ~Session();
    void begin();
    void http_request();
    void respond(http::status status, const Json& value);
    void read();
    void message();
    void run_codec(Json start);
    void control(Json value);
    void credit(uint64_t bytes);
    void enqueue(Message value);
    void pump();
    void fail(const std::string& code, const Json& evidence = Json::object());
    void stop();
    void tick();
    void close_after_write();
    static Message json_message(const Json& value, bool terminal = false) {
        auto text = value.dump(); return {true, {text.begin(), text.end()}, terminal, value.value("type", "") == "started"};
    }
};
struct Server {
    Config config;
    Logger logger;
    net::io_context io{2};
    tcp::acceptor acceptor{io};
    net::thread_pool codecs;
    std::atomic<int> slots{0};
    std::vector<std::weak_ptr<Session>> sessions; // acceptor strand only, shutdown after acceptor cancel
    net::strand<net::io_context::executor_type> accept_strand{net::make_strand(io)};
    net::signal_set signals{io, SIGINT, SIGTERM};
    std::atomic<bool> shutting_down{false};
    std::atomic<bool> shutdown_done{false};
    net::executor_work_guard<net::io_context::executor_type> work_guard{io.get_executor()};
    net::steady_timer shutdown_timer{accept_strand};
    void await_workers() {
        if (slots.load() == 0) { work_guard.reset(); signals.cancel(); return; }
        shutdown_timer.expires_after(std::chrono::milliseconds(50));
        shutdown_timer.async_wait([this](auto error) { if (!error) await_workers(); });
    }
    explicit Server(const Config& c) : config(c), logger(c), codecs(c.get("sessions", "maximum")) {
        auto endpoint = tcp::endpoint(net::ip::make_address(config.value["listen"]["address"].get<std::string>()), config.get("listen", "port"));
        acceptor.open(endpoint.protocol()); acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind(endpoint); acceptor.listen();
    }
    bool reserve() {
        int count = slots.load();
        while (count < config.get("sessions", "maximum")) if (slots.compare_exchange_weak(count, count + 1)) return true;
        return false;
    }
    void accept() {
        acceptor.async_accept(net::make_strand(io), net::bind_executor(accept_strand, [this](beast::error_code error, tcp::socket socket) {
            if (!error) {
                sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [](auto& s) { return s.expired(); }), sessions.end());
                // Bound pre-upgrade sockets too: sessions with no start cannot exhaust RAM.
                if (sessions.size() < 2 * static_cast<size_t>(config.get("sessions", "maximum")) + 16) {
                    auto session = std::make_shared<Session>(*this, std::move(socket), config);
                    sessions.push_back(session); session->begin();
                }
            }
            if (acceptor.is_open()) accept();
        }));
    }
    void shutdown() {
        if (shutting_down.exchange(true)) return;
        net::post(accept_strand, [this] {
            acceptor.close();
            for (auto& weak : sessions) if (auto s = weak.lock()) net::post(s->socket.get_executor(), [s] { s->fail("server_shutdown"); });
            await_workers();
        });
    }
    int run() {
        accept();
        signals.async_wait([this](auto error, int) { if (!error) shutdown(); });
        // A stuck codec must be reported as a process failure, never claimed cleaned up.
        std::thread watchdog([this] {
            while (!shutting_down && !shutdown_done) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto deadline = Clock::now() + std::chrono::seconds(10);
            while (!shutdown_done && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!shutdown_done) { std::cerr << "fatal: shutdown_timeout\n"; std::_Exit(2); }
        });
        std::thread other([this] { io.run(); }); io.run(); other.join();
        codecs.join(); shutdown_done = true; watchdog.join(); return 0;
    }
};
Session::Session(Server& o, tcp::socket s, const Config& c)
    : owner(o), config(c), socket(std::move(s)), timer(socket.get_executor()), error_flush_timer(socket.get_executor()),
      inputs(c.get("limits", "queued_input_bytes")), input_limit(c.get("limits", "input_window_bytes")) {
    request.header_limit(16384); request.body_limit(0);
}
Session::~Session() { if (slot) --owner.slots; }
void Session::begin() {
    beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(config.get("timeouts", "handshake_seconds")));
    auto self = shared_from_this();
    http::async_read(socket.next_layer(), read_buffer, request, [self](auto error, size_t) {
        if (error) return self->stop();
        try { self->http_request(); } catch (...) { self->respond(http::status::bad_request, {{"code", "protocol_error"}}); }
    });
}
void Session::respond(http::status status, const Json& value) {
    auto response = std::make_shared<http::response<http::string_body>>(status, 11);
    response->set(http::field::content_type, "application/json"); response->keep_alive(false);
    response->body() = value.dump(); response->prepare_payload();
    http::async_write(socket.next_layer(), *response, [self = shared_from_this(), response](auto, size_t) { self->stop(); });
}
void Session::http_request() {
    auto& req = request.get();
    if (req.method() != http::verb::get) return respond(http::status::method_not_allowed, {{"code", "protocol_error"}});
    if (req.find(http::field::origin) != req.end()) return respond(http::status::bad_request, {{"code", "protocol_error"}});
    if (req.target() == "/healthz") return respond(http::status::ok, {{"status", "ok"}, {"protocol", 1}});
    if (req.target() == "/v1/capabilities") return respond(http::status::ok, {
        {"protocol", 1}, {"service_version", CSAD_SERVICE_VERSION}, {"build_id", CSAD_BUILD_ID}, {"ffmpeg_revision", revision},
        {"input_codecs", {"dts", "truehd", "mlp"}}, {"output_codecs", {"flac"}},
        {"output_layout_policies", {"dts-side-5.1-to-7.1-silence-v1", "dts-native-or-side-7.1-v1"}},
        {"limits", config.value["limits"]}, {"available_sessions", std::max(0, config.get("sessions", "maximum") - owner.slots.load())}});
    if (req.target() != "/v1/session") return respond(http::status::not_found, {{"code", "protocol_error"}});
    if (!ws::is_upgrade(req) || req[http::field::sec_websocket_protocol] != "cs-audio.v1")
        return respond(http::status::bad_request, {{"code", "unsupported_version"}});
    if (owner.shutting_down || !owner.reserve()) return respond(http::status::service_unavailable, {{"code", "capacity"}});
    slot = true;
    socket.set_option(ws::stream_base::decorator([](ws::response_type& res) { res.set(http::field::sec_websocket_protocol, "cs-audio.v1"); }));
    socket.set_option(ws::stream_base::timeout{std::chrono::seconds(config.get("timeouts", "handshake_seconds")), ws::stream_base::none(), false});
    socket.read_message_max(config.get("limits", "packet_payload_bytes") + 64);
    std::weak_ptr<Session> weak = shared_from_this();
    socket.control_callback([weak](ws::frame_type type, beast::string_view) {
        if (auto self = weak.lock(); self && type == ws::frame_type::pong) self->awaiting_pong = false;
    });
    beast::get_lowest_layer(socket).expires_never();
    socket.async_accept(req, [self = shared_from_this()](auto error) {
        if (error) return self->stop();
        self->upgraded = true; self->opened = Clock::now();
        self->control({{"type", "hello"}, {"protocol", 1}, {"session_id", self->id}, {"service_version", CSAD_SERVICE_VERSION},
            {"limits", {{"packet_payload_bytes", self->config.get("limits", "packet_payload_bytes")}, {"control_message_bytes", self->config.get("limits", "control_message_bytes")}}},
            {"input_limit_bytes", std::to_string(self->input_limit)}});
        self->read(); self->tick();
    });
}
void Session::read() {
    if (closing || cancelled) return;
    socket.async_read(read_buffer, [self = shared_from_this()](auto error, size_t) {
        if (error) return self->stop();
        try { self->message(); }
        catch (const Failure& error) { self->fail(error.what()); }
        catch (const Json::exception&) { self->fail("protocol_error"); }
        catch (...) { self->fail("internal_error"); }
        self->read_buffer.consume(self->read_buffer.size()); self->read();
    });
}
void Session::message() {
    if (!socket.got_text()) {
        require(activated && !eof);
        auto size = read_buffer.size(); require(size <= input_limit - received_bytes, "limit_exceeded");
        std::vector<uint8_t> bytes(size); net::buffer_copy(net::buffer(bytes), read_buffer.data());
        auto p = parse_packet(bytes, 1, generation, received, config.get("limits", "packet_payload_bytes"));
        auto owned = input_owned.fetch_add(size) + size;
        auto peak = input_peak.load(); while (owned > peak && !input_peak.compare_exchange_weak(peak, owned)) {}
        require(inputs.try_push(Work{std::move(p)}, size), "limit_exceeded");
        received = add(received, 1); received_bytes = add(received_bytes, size); return;
    }
    Json value = parse_json(beast::buffers_to_string(read_buffer.data()), config.get("limits", "control_message_bytes"));
    if (value.contains("required_features")) require(value["required_features"].is_array() && value["required_features"].empty());
    std::string type = value.at("type").get<std::string>();
    if (type == "cancel") {
        require(generation && decimal<uint64_t>(value.at("generation")) == generation);
        cancelled = true; inputs.cancel(); space.notify_all();
        { std::lock_guard lock(mutex); outputs.clear(); controls.clear(); pending_credit.reset(); }
        control({{"type", "cancelled"}, {"generation", std::to_string(generation)}}); closing = true;
        timer.expires_after(std::chrono::seconds(config.get("timeouts", "close_seconds")));
        timer.async_wait([self = shared_from_this()](auto error) { if (!error) self->stop(); });
        pump(); return;
    }
    if (type == "start") {
        require(!started); generation = decimal<uint64_t>(value.at("generation")); require(generation && generation <= INT64_MAX);
        auto limit = decimal<uint64_t>(value.at("output_limit_bytes"));
        require(limit >= static_cast<uint64_t>(config.get("limits", "packet_payload_bytes") + 64) && limit <= static_cast<uint64_t>(config.get("limits", "queued_output_bytes")));
        output_credit.limit = limit; started = true; stage_started = Clock::now(); working = true;
        net::post(owner.codecs, [self = shared_from_this(), value] { self->run_codec(value); }); return;
    }
    require(started);
    if (type == "credit") {
        require(value.at("direction") == "output"); output_credit.grant(decimal<uint64_t>(value.at("limit_bytes"))); pump();
    } else if (type == "end_input") {
        require(!eof && decimal<uint64_t>(value.at("generation")) == generation && decimal<uint64_t>(value.at("input_packets")) == received);
        require(inputs.try_push(Work{}, 0), "limit_exceeded"); eof = true; stage_started = Clock::now();
    } else throw Failure("protocol_error");
}
void Session::run_codec(Json start) {
    owner.logger.event({{"event", "session_start"}, {"session_id", id}});
    auto began = Clock::now();
    try {
        auto self = shared_from_this();
        CodecPipeline pipeline(start, config.get("sessions", "codec_threads"), config.get("limits", "pcm_fifo_samples"),
            [this](const Json& format) {
                formatted = true; owner.logger.event({{"event", "output_format"}, {"session_id", id},
                    {"sample_rate", format.at("sample_rate")}, {"bits_per_sample", format.at("bits_per_sample")},
                    {"channel_order", format.at("channel_order")}, {"source_profile", format.at("source_profile")}}); control(format);
            },
            [this](Packet packet) { require(packet.payload.size() <= static_cast<size_t>(config.get("limits", "packet_payload_bytes")), "limit_exceeded"); enqueue({false, serialize(packet)}); }, [this] { return cancelled.load(); });
        control({{"type", "started"}, {"generation", std::to_string(generation)}});
        for (;;) {
            working = false;
            auto work = inputs.pop(); if (!work || cancelled) break;
            working = true; progress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
            if (!work->packet) { auto end = pipeline.finish();
                owner.logger.event({{"event", "output_summary"}, {"session_id", id},
                    {"input_packets", end.at("input_packets")}, {"output_packets", end.at("output_packets")},
                    {"total_samples", end.at("total_samples")}});
                enqueue(json_message(end, true)); break; }
            auto bytes = work->packet->payload.size() + 64;
            credit(bytes); pipeline.submit(*work->packet); input_owned.fetch_sub(bytes);
            progress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        }
    } catch (const Failure& error) {
        std::string code = error.what();Json evidence=error.evidence;
        net::post(socket.get_executor(), [self = shared_from_this(), code, evidence] { if (!self->cancelled) self->fail(code,evidence); });
    } catch (const Json::exception&) {
        net::post(socket.get_executor(), [self = shared_from_this()] { self->fail("protocol_error"); });
    } catch (...) {
        net::post(socket.get_executor(), [self = shared_from_this()] { self->fail("internal_error"); });
    }
    working = false;
    owner.logger.event({{"event", "session_end"}, {"session_id", id}, {"input_high_water_bytes", std::to_string(input_peak.load())}, {"output_high_water_bytes", std::to_string(output_peak)}, {"wall_ms", std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count()}});
}
void Session::control(Json value) {
    {
        std::lock_guard lock(mutex);
        if (controls.size() >= 32) { net::post(socket.get_executor(), [self = shared_from_this()] { self->stop(); }); return; }
        controls.push_back(json_message(value));
    }
    net::post(socket.get_executor(), [self = shared_from_this()] { self->pump(); });
}
void Session::credit(uint64_t bytes) {
    // Worker consumes one owned packet; the network strand alone owns counters.
    net::post(socket.get_executor(), [self = shared_from_this(), bytes] {
        if (self->cancelled) return;
        try {
            self->consumed_bytes = add(self->consumed_bytes, bytes);
            self->input_limit = add(self->consumed_bytes, self->config.get("limits", "input_window_bytes"));
            { std::lock_guard lock(self->mutex); self->pending_credit = json_message({{"type", "credit"}, {"direction", "input"}, {"limit_bytes", std::to_string(self->input_limit)}}); }
            self->pump();
        } catch (const Failure& e) { self->fail(e.what()); }
    });
}
void Session::enqueue(Message value) {
    size_t size = value.bytes.size();
    require(size <= static_cast<size_t>(config.get("limits", "queued_output_bytes")), "limit_exceeded");
    std::unique_lock lock(mutex); waiting_output = true;
    space.wait(lock, [&] { return cancelled || (size <= config.get("limits", "queued_output_bytes") - output_bytes && outputs.size() < 4096); });
    progress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    waiting_output = false; require(!cancelled, "cancelled");
    output_bytes += size; output_peak = std::max(output_peak, output_bytes); outputs.push_back(std::move(value)); lock.unlock();
    net::post(socket.get_executor(), [self = shared_from_this()] { self->pump(); });
}
void Session::pump() {
    if (!upgraded || writing || pinging) return;
    {
        std::lock_guard lock(mutex);
        if (!controls.empty()) { writing = std::make_shared<Message>(std::move(controls.front())); controls.pop_front(); }
        else if (pending_credit) { writing = std::make_shared<Message>(std::move(*pending_credit)); pending_credit.reset(); }
        else if (!outputs.empty() && (outputs.front().text || output_credit.fits(outputs.front().bytes.size()))) {
            writing = std::make_shared<Message>(std::move(outputs.front())); outputs.pop_front();
            if (!writing->text) output_credit.spend(writing->bytes.size());
        }
    }
    if (!writing) { if (closing) close_after_write(); return; }
    write_started = Clock::now();
    socket.text(writing->text);
    auto message = writing;
    socket.async_write(net::buffer(message->bytes), [self = shared_from_this(), message](auto error, size_t) {
        if (error) return self->stop();
        if (!message->text || message->terminal) {
            { std::lock_guard lock(self->mutex); self->output_bytes -= message->bytes.size(); }
            self->space.notify_all();
        }
        if (message->activates) self->activated = true;
        self->writing.reset();
        // end_output is a barrier. Wait for peer's normal close, continue heartbeat.
        self->pump();
    });
}
void Session::fail(const std::string& code, const Json& evidence) {
    if (cancelled.exchange(true)) return;
    owner.logger.event({{"event", "session_error"}, {"session_id", id}, {"code", code}});
    inputs.cancel(); space.notify_all(); closing = true; flush_error = true;
    close_code = code == "protocol_error" || code == "invalid_packet" || code == "unsupported_version" ? ws::close_code::protocol_error : code == "limit_exceeded" ? ws::close_code::too_big : ws::close_code::internal_error;
    { std::lock_guard lock(mutex); outputs.clear(); controls.clear(); pending_credit.reset(); }
    if (!upgraded) return stop();
    Json message={{"type", "error"}, {"generation", std::to_string(generation)}, {"code", code},
             {"retryable", false}, {"message", "Audio conversion could not continue."}};
    if(code=="timeline_discontinuity"&&!evidence.empty())message["timeline"]=evidence;
    control(message);
    timer.expires_after(std::chrono::seconds(config.get("timeouts", "close_seconds")));
    timer.async_wait([self = shared_from_this()](auto e) { if (!e) self->stop(); });
    pump();
}
void Session::close_after_write() {
    if (close_started) return;
    close_started = true;
    if (!socket.is_open()) return stop();
    auto close = [self=shared_from_this()] {
        if (!self->socket.is_open()) return self->stop();
        self->socket.async_close(self->close_code,[self](auto){self->stop();});
    };
    if (flush_error) {
        // Let URLSession consume the completed application error write before
        // its close callback can overtake delivery. The existing close deadline
        // remains authoritative; peer cancellation still tears down immediately.
        error_flush_timer.expires_after(std::chrono::milliseconds(100));
        error_flush_timer.async_wait([close](auto error){if(!error)close();});
    } else close();
}
void Session::stop() {
    cancelled = true; closing = true; inputs.cancel(); space.notify_all();
    beast::error_code error; timer.cancel(); error_flush_timer.cancel();
    beast::get_lowest_layer(socket).socket().cancel(error);
    beast::get_lowest_layer(socket).socket().shutdown(tcp::socket::shutdown_both, error);
    beast::get_lowest_layer(socket).socket().close(error);
}
void Session::tick() {
    if (cancelled) return;
    auto now = Clock::now();
    if (!started && now - opened >= std::chrono::seconds(config.get("timeouts", "start_seconds"))) return fail("timeout");
    if (awaiting_pong && now >= pong_due) return fail("timeout");
    if (writing && now - write_started >= std::chrono::seconds(config.get("timeouts", "ping_seconds") + config.get("timeouts", "pong_seconds"))) return fail("timeout");
    // No data-idle timeout: waiting for input/credit is a valid paused session.
    if (working && !waiting_output && progress_ns.load() > 0) {
        auto last = Clock::time_point(std::chrono::nanoseconds(progress_ns.load()));
        if (now - last > std::chrono::seconds(config.get("timeouts", eof ? "drain_seconds" : "format_seconds"))) return fail("timeout");
    }
    if (now - ping_at >= std::chrono::seconds(config.get("timeouts", "ping_seconds")) && !writing && !pinging) {
        pinging = true; ping_at = now; awaiting_pong = true; pong_due = now + std::chrono::seconds(config.get("timeouts", "pong_seconds"));
        socket.async_ping({}, [self = shared_from_this()](auto error) {
            self->pinging = false;
            if (error) return self->stop();
            self->pump();
        });
    }
    timer.expires_after(std::chrono::milliseconds(100));
    timer.async_wait([self = shared_from_this()](auto error) { if (!error) self->tick(); });
}
int serve(const Config& config) { CodecPipeline::selfcheck(); return Server(config).run(); }
int healthcheck(const Config& config) {
    try {
        net::io_context io; beast::tcp_stream stream(io); stream.expires_after(std::chrono::seconds(2));
        auto address = net::ip::make_address(config.value["listen"]["address"].get<std::string>());
        if (address.is_unspecified()) address = address.is_v4() ? net::ip::address(net::ip::address_v4::loopback()) : net::ip::address(net::ip::address_v6::loopback());
        stream.connect({address, static_cast<unsigned short>(config.get("listen", "port"))});
        http::request<http::empty_body> request{http::verb::get, "/healthz", 11}; request.set(http::field::host, "localhost");
        http::write(stream, request); beast::flat_buffer buffer; http::response_parser<http::string_body> response;
        response.header_limit(16384); response.body_limit(65536); http::read(stream, buffer, response);
        return response.get().result() == http::status::ok && parse_json(response.get().body()).at("status") == "ok" ? 0 : 1;
    } catch (...) { return 1; }
}
}
