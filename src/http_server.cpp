#include "cspromator/http_server.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#endif

namespace cspromator {
namespace {

void close_socket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

struct SocketRuntime {
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

std::string lower_copy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::optional<std::size_t> parse_content_length(std::string_view headers) {
    std::size_t begin = 0;
    while (begin < headers.size()) {
        const auto end = headers.find("\r\n", begin);
        const auto line_end = end == std::string_view::npos ? headers.size() : end;
        const auto line = headers.substr(begin, line_end - begin);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            const auto name = lower_copy(line.substr(0, colon));
            if (name == "content-length") {
                auto value = line.substr(colon + 1);
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
                    value.remove_prefix(1);
                }
                std::size_t parsed{};
                const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (result.ec == std::errc{}) {
                    return parsed;
                }
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 2;
    }
    return std::nullopt;
}

bool send_all(socket_t sock, std::string_view data) {
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
#ifdef _WIN32
        const int sent = send(sock, data.data() + sent_total,
                              static_cast<int>(data.size() - sent_total), 0);
#else
        const auto sent = send(sock, data.data() + sent_total, data.size() - sent_total, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

struct HttpRequest {
    std::string method;
    std::string body;
};

std::optional<HttpRequest> receive_request(socket_t client) {
    constexpr std::size_t max_header_bytes = 64 * 1024;
    constexpr std::size_t max_body_bytes = 4 * 1024 * 1024;

    std::string buffer;
    buffer.reserve(16 * 1024);
    std::array<char, 8192> chunk{};
    std::size_t header_end = std::string::npos;

    while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
#ifdef _WIN32
        const int received = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
#else
        const auto received = recv(client, chunk.data(), chunk.size(), 0);
#endif
        if (received <= 0) {
            return std::nullopt;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(received));
        if (buffer.size() > max_header_bytes) {
            return std::nullopt;
        }
    }

    const std::string_view headers(buffer.data(), header_end);
    const auto first_space = headers.find(' ');
    if (first_space == std::string_view::npos) {
        return std::nullopt;
    }
    std::string method(headers.substr(0, first_space));

    const auto length = parse_content_length(headers);
    if (!length || *length > max_body_bytes) {
        return std::nullopt;
    }

    const std::size_t body_start = header_end + 4;
    while (buffer.size() - body_start < *length) {
#ifdef _WIN32
        const int received = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
#else
        const auto received = recv(client, chunk.data(), chunk.size(), 0);
#endif
        if (received <= 0) {
            return std::nullopt;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(received));
        if (buffer.size() - body_start > max_body_bytes) {
            return std::nullopt;
        }
    }

    return HttpRequest{std::move(method), buffer.substr(body_start, *length)};
}

} // namespace

GsiHttpServer::GsiHttpServer(std::uint16_t port,
                             const MonotonicClock& clock,
                             SessionRecorder& recorder,
                             LiveEventPipeline& live_events)
    : port_(port), clock_(clock), recorder_(recorder), live_events_(live_events) {}

void GsiHttpServer::request_stop() {
    stop_requested_.store(true);
}

int GsiHttpServer::run() {
    SocketRuntime runtime;

    const socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) {
        throw std::runtime_error("Could not create TCP socket");
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(server);
        throw std::runtime_error("Could not bind 127.0.0.1:" + std::to_string(port_));
    }
    if (listen(server, 8) != 0) {
        close_socket(server);
        throw std::runtime_error("Could not listen on local socket");
    }

    std::cout << "[PROMATOR] GSI probe listening on http://127.0.0.1:" << port_ << "/\n";
    std::cout << "[PROMATOR] Session: " << recorder_.directory().string() << "\n";
    std::cout << "[PROMATOR] Live event pipeline enabled.\n";
    std::cout << "[PROMATOR] Press Ctrl+C to stop.\n";

    while (!stop_requested_.load()) {
        // Poll the listening socket with a short timeout so Ctrl+C can request
        // a graceful stop and allow both worker queues to drain.
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(server, &read_set);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100'000;
#ifdef _WIN32
        const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
#else
        const int ready = select(server + 1, &read_set, nullptr, nullptr, &timeout);
#endif
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (stop_requested_.load()) {
                break;
            }
            continue;
        }

        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        const socket_t client = accept(server, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == invalid_socket) {
            if (stop_requested_.load()) {
                break;
            }
            continue;
        }

        // The earliest timestamp this prototype can honestly claim: local TCP accept.
        const auto accepted_tick = clock_.now();
        auto request = receive_request(client);
        const auto body_complete_tick = clock_.now();

        if (!request) {
            constexpr std::string_view bad =
                "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send_all(client, bad);
            close_socket(client);
            continue;
        }

        if (request->method != "POST") {
            constexpr std::string_view not_allowed =
                "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send_all(client, not_allowed);
            close_socket(client);
            continue;
        }

        // Acknowledge CS2 before persistence, JSON parsing, event detection, or
        // any director work.
        constexpr std::string_view ok =
            "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send_all(client, ok);
        const auto ack_sent_tick = clock_.now();
        close_socket(client);

        // Fan the immutable payload into independent queues. The only extra
        // network-thread work is a small string copy plus two queue pushes.
        std::string live_body = request->body;
        const auto record = recorder_.enqueue(
            accepted_tick, body_complete_tick, ack_sent_tick, std::move(request->body));
        live_events_.enqueue(record.sequence, record.relative_us, std::move(live_body));

        const double ingress_ms = clock_.seconds_between(accepted_tick, body_complete_tick) * 1000.0;
        const double ack_ms = clock_.seconds_between(accepted_tick, ack_sent_tick) * 1000.0;
        std::cout << "[GSI] #" << record.sequence
                  << " t+" << (record.relative_us / 1000.0) << " ms"
                  << " bytes=" << record.body_bytes
                  << " ingress=" << ingress_ms << " ms"
                  << " ack=" << ack_ms << " ms"
                  << " queued\n";
    }

    close_socket(server);
    return 0;
}

} // namespace cspromator
