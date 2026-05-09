#include "bt/model_service.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)

namespace bt {

std::unique_ptr<model_service_client> make_websocket_model_service_client(model_service_config config) {
    (void)config;
    return std::make_unique<unavailable_model_service_client>();
}

bool model_service_bridge_target_available() noexcept {
    return false;
}

}  // namespace bt

#else

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace bt {
namespace {

struct parsed_ws_endpoint {
    std::string host;
    std::string port = "80";
    std::string path = "/v1/ws";
};

class socket_handle {
public:
    socket_handle() = default;
    explicit socket_handle(int fd) : fd_(fd) {}
    socket_handle(const socket_handle&) = delete;
    socket_handle& operator=(const socket_handle&) = delete;
    socket_handle(socket_handle&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    socket_handle& operator=(socket_handle&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~socket_handle() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

    void reset() noexcept {
        if (fd_ >= 0) {
            (void)::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

parsed_ws_endpoint parse_endpoint(const std::string& endpoint) {
    constexpr std::string_view prefix = "ws://";
    if (endpoint.rfind(prefix, 0) != 0) {
        throw std::runtime_error("only ws:// muesli-model-service endpoints are supported");
    }
    parsed_ws_endpoint out;
    const std::string_view rest(endpoint.data() + prefix.size(), endpoint.size() - prefix.size());
    const std::size_t slash = rest.find('/');
    const std::string_view authority = slash == std::string_view::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string_view::npos) {
        out.host = std::string(authority);
    } else {
        out.host = std::string(authority.substr(0, colon));
        out.port = std::string(authority.substr(colon + 1));
    }
    if (out.host.empty()) {
        throw std::runtime_error("model-service endpoint is missing a host");
    }
    if (out.path.empty()) {
        out.path = "/";
    }
    return out;
}

void set_socket_timeouts(int fd, std::int64_t timeout_ms) {
    const auto clamped_ms = std::max<std::int64_t>(timeout_ms, 1);
    timeval tv{};
    tv.tv_sec = static_cast<long>(clamped_ms / 1000);
    tv.tv_usec = static_cast<int>((clamped_ms % 1000) * 1000);
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

socket_handle connect_tcp(const parsed_ws_endpoint& endpoint, std::int64_t connect_timeout_ms) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw_results = nullptr;
    const int gai = ::getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &raw_results);
    if (gai != 0) {
        throw std::runtime_error(std::string("model-service DNS resolution failed: ") + ::gai_strerror(gai));
    }
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(raw_results, ::freeaddrinfo);

    for (addrinfo* ai = results.get(); ai != nullptr; ai = ai->ai_next) {
        socket_handle sock(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!sock) {
            continue;
        }

        const int flags = ::fcntl(sock.get(), F_GETFL, 0);
        if (flags >= 0) {
            (void)::fcntl(sock.get(), F_SETFL, flags | O_NONBLOCK);
        }

        const int rc = ::connect(sock.get(), ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno != EINPROGRESS) {
            continue;
        }

        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock.get(), &write_set);
        timeval tv{};
        const auto clamped_ms = std::max<std::int64_t>(connect_timeout_ms, 1);
        tv.tv_sec = static_cast<long>(clamped_ms / 1000);
        tv.tv_usec = static_cast<int>((clamped_ms % 1000) * 1000);
        const int ready = ::select(sock.get() + 1, nullptr, &write_set, nullptr, &tv);
        if (ready <= 0) {
            continue;
        }
        int error = 0;
        socklen_t error_len = sizeof(error);
        if (::getsockopt(sock.get(), SOL_SOCKET, SO_ERROR, &error, &error_len) != 0 || error != 0) {
            continue;
        }
        if (flags >= 0) {
            (void)::fcntl(sock.get(), F_SETFL, flags);
        }
        return sock;
    }

    throw std::runtime_error("model-service TCP connection failed");
}

void write_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            throw std::runtime_error("model-service socket write failed");
        }
        sent += static_cast<std::size_t>(n);
    }
}

std::vector<std::uint8_t> read_exact(int fd, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    std::size_t read = 0;
    while (read < count) {
        const ssize_t n = ::recv(fd, out.data() + read, count - read, 0);
        if (n <= 0) {
            throw std::runtime_error("model-service socket read failed");
        }
        read += static_cast<std::size_t>(n);
    }
    return out;
}

std::string read_http_headers(int fd) {
    std::string out;
    std::array<char, 1> ch{};
    while (out.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = ::recv(fd, ch.data(), ch.size(), 0);
        if (n <= 0) {
            throw std::runtime_error("model-service websocket handshake read failed");
        }
        out.push_back(ch[0]);
        if (out.size() > 16384) {
            throw std::runtime_error("model-service websocket handshake too large");
        }
    }
    return out;
}

std::uint32_t random_u32() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng();
}

std::array<std::uint8_t, 4> make_mask() {
    const std::uint32_t value = random_u32();
    return {static_cast<std::uint8_t>((value >> 24) & 0xff), static_cast<std::uint8_t>((value >> 16) & 0xff),
            static_cast<std::uint8_t>((value >> 8) & 0xff), static_cast<std::uint8_t>(value & 0xff)};
}

void send_text_frame(int fd, const std::string& text) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    const auto mask = make_mask();
    const std::size_t size = text.size();
    if (size <= 125) {
        frame.push_back(static_cast<char>(0x80 | size));
    } else if (size <= 0xffff) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((size >> 8) & 0xff));
        frame.push_back(static_cast<char>(size & 0xff));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((size >> shift) & 0xff));
        }
    }
    for (std::uint8_t byte : mask) {
        frame.push_back(static_cast<char>(byte));
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        frame.push_back(static_cast<char>(static_cast<std::uint8_t>(text[i]) ^ mask[i % mask.size()]));
    }
    write_all(fd, frame);
}

std::uint64_t read_payload_length(int fd, std::uint8_t first_len) {
    std::uint64_t length = first_len & 0x7f;
    if (length == 126) {
        const auto bytes = read_exact(fd, 2);
        length = (static_cast<std::uint64_t>(bytes[0]) << 8) | bytes[1];
    } else if (length == 127) {
        const auto bytes = read_exact(fd, 8);
        length = 0;
        for (std::uint8_t byte : bytes) {
            length = (length << 8) | byte;
        }
    }
    return length;
}

std::string read_text_frame(int fd) {
    while (true) {
        const auto header = read_exact(fd, 2);
        const std::uint8_t opcode = header[0] & 0x0f;
        const bool masked = (header[1] & 0x80) != 0;
        const std::uint64_t length = read_payload_length(fd, header[1]);
        if (length > 16 * 1024 * 1024) {
            throw std::runtime_error("model-service websocket frame too large");
        }
        std::array<std::uint8_t, 4> mask{};
        if (masked) {
            const auto mask_bytes = read_exact(fd, 4);
            std::copy(mask_bytes.begin(), mask_bytes.end(), mask.begin());
        }
        auto payload = read_exact(fd, static_cast<std::size_t>(length));
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % mask.size()];
            }
        }
        if (opcode == 0x1) {
            return std::string(payload.begin(), payload.end());
        }
        if (opcode == 0x8) {
            throw std::runtime_error("model-service websocket closed");
        }
        if (opcode == 0x9) {
            // Ignore pings for this one-request client; the next recv will read the response.
            continue;
        }
    }
}

void websocket_handshake(int fd, const parsed_ws_endpoint& endpoint) {
    constexpr std::string_view key = "bXVlc2xpLWJ0LWJyaWRnZQ==";
    std::ostringstream request;
    request << "GET " << endpoint.path << " HTTP/1.1\r\n"
            << "Host: " << endpoint.host << ':' << endpoint.port << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
    write_all(fd, request.str());
    const std::string response = read_http_headers(fd);
    if (response.rfind("HTTP/1.1 101", 0) != 0 && response.rfind("HTTP/1.0 101", 0) != 0) {
        throw std::runtime_error("model-service websocket handshake was not accepted");
    }
}

model_service_response unavailable_from_exception(const model_service_request& request, const std::exception& error) {
    model_service_response out;
    out.id = request.id;
    out.status = model_service_status::unavailable;
    out.error_code = "model_service_unavailable";
    out.error_message = error.what();
    out.error_retryable = true;
    out.host_reached = false;
    return out;
}

class websocket_model_service_client final : public model_service_client {
public:
    explicit websocket_model_service_client(model_service_config config) : config_(std::move(config)) {}

    [[nodiscard]] model_service_response call(const model_service_request& request) override {
        try {
            const parsed_ws_endpoint endpoint = parse_endpoint(config_.endpoint);
            socket_handle sock = connect_tcp(endpoint, config_.connect_timeout_ms);
            set_socket_timeouts(sock.get(), config_.request_timeout_ms);
            websocket_handshake(sock.get(), endpoint);
            send_text_frame(sock.get(), model_service_request_to_json(request));
            model_service_response response = model_service_response_from_json(read_text_frame(sock.get()));
            if (response.id.empty()) {
                response.id = request.id;
            }
            response.host_reached = false;
            return response;
        } catch (const std::exception& error) {
            return unavailable_from_exception(request, error);
        }
    }

private:
    model_service_config config_;
};

}  // namespace

std::unique_ptr<model_service_client> make_websocket_model_service_client(model_service_config config) {
    return std::make_unique<websocket_model_service_client>(std::move(config));
}

bool model_service_bridge_target_available() noexcept {
    return true;
}

}  // namespace bt

#endif
