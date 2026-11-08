#include "chronokit/server/http_server.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
static void close_sock(int fd) { closesocket(fd); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static void close_sock(int fd) { ::close(fd); }
#endif

namespace chronokit {

namespace {

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+' ) {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            int hi = 0, lo = 0;
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            hi = hex(in[i + 1]);
            lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

void parse_query_string(const std::string& qs, std::map<std::string, std::string>& out) {
    size_t start = 0;
    while (start < qs.size()) {
        size_t amp = qs.find('&', start);
        std::string pair = qs.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            out[url_decode(pair)] = "";
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
}

std::string status_reason(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

void HttpServer::route(const std::string& method, const std::string& path, Handler handler) {
    routes_.push_back({method, path, std::move(handler)});
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        close_sock(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::listen_and_serve(const std::string& host, uint16_t port) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    listen_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listen_fd_ < 0) {
        throw std::runtime_error("Failed to create TCP socket");
    }

    int opt = 1;
#if defined(_WIN32)
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_sock(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("Failed to bind port " + std::to_string(port));
    }

    if (::listen(listen_fd_, 64) < 0) {
        close_sock(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("Failed to listen on socket");
    }

    running_ = true;
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int cfd = static_cast<int>(::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len));
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }
        std::thread(&HttpServer::handle_client, this, cfd).detach();
    }
}

void HttpServer::handle_client(int client_fd) const {
    std::string raw;
    char buf[8192];
    size_t header_end = std::string::npos;
    size_t content_len = 0;

    while (true) {
        auto n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));

        if (header_end == std::string::npos) {
            header_end = raw.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string hdr_part = raw.substr(0, header_end);
                std::istringstream iss(hdr_part);
                std::string line;
                std::getline(iss, line);
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string k = to_lower_str(line.substr(0, colon));
                        std::string v = line.substr(colon + 1);
                        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                        if (k == "content-length") {
                            try { content_len = static_cast<size_t>(std::stoul(v)); } catch (...) { content_len = 0; }
                        }
                    }
                }
            }
        }

        if (header_end != std::string::npos && raw.size() >= header_end + 4 + content_len) {
            break;
        }
        if (raw.size() > 4 * 1024 * 1024) break; // 4MB cap
    }

    if (header_end == std::string::npos) {
        close_sock(client_fd);
        return;
    }

    HttpRequest req;
    std::istringstream iss(raw.substr(0, header_end));
    std::string req_line;
    std::getline(iss, req_line);
    if (!req_line.empty() && req_line.back() == '\r') req_line.pop_back();

    {
        std::istringstream rls(req_line);
        std::string full_target;
        rls >> req.method >> full_target;
        auto qpos = full_target.find('?');
        if (qpos != std::string::npos) {
            req.path = full_target.substr(0, qpos);
            parse_query_string(full_target.substr(qpos + 1), req.query);
        } else {
            req.path = full_target;
        }
    }

    std::string hline;
    while (std::getline(iss, hline)) {
        if (!hline.empty() && hline.back() == '\r') hline.pop_back();
        auto colon = hline.find(':');
        if (colon != std::string::npos) {
            std::string k = to_lower_str(hline.substr(0, colon));
            std::string v = hline.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            req.headers[k] = v;
        }
    }

    req.body = raw.substr(header_end + 4, content_len);

    HttpResponse resp;
    if (req.method == "OPTIONS") {
        resp.status = 204;
        resp.body = "";
    } else {
        bool matched = false;
        for (const auto& rt : routes_) {
            if (rt.method == req.method && rt.path == req.path) {
                matched = true;
                try {
                    resp = rt.handler(req);
                } catch (const std::exception& ex) {
                    resp = HttpResponse::json(JsonValue::object({
                        {"ok", false},
                        {"error_code", "SERVER_EXCEPTION"},
                        {"message", ex.what()}
                    }), 400);
                }
                break;
            }
        }
        if (!matched) {
            resp = HttpResponse::json(JsonValue::object({
                {"ok", false},
                {"error_code", "NOT_FOUND"},
                {"message", "Route not found: " + req.method + " " + req.path}
            }), 404);
        }
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status << " " << status_reason(resp.status) << "\r\n"
        << "Content-Type: " << resp.content_type << "\r\n"
        << "Content-Length: " << resp.body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Connection: close\r\n\r\n"
        << resp.body;

    std::string wire = oss.str();
    size_t total_sent = 0;
    while (total_sent < wire.size()) {
        auto sent = ::send(client_fd, wire.data() + total_sent, wire.size() - total_sent, 0);
        if (sent <= 0) break;
        total_sent += static_cast<size_t>(sent);
    }

    close_sock(client_fd);
}

} // namespace chronokit
