#pragma once

#include "chronokit/core/json.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace chronokit {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
    std::string body;

    JsonValue json() const {
        if (body.empty()) return JsonValue::object();
        return JsonValue::parse(body);
    }
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;

    static HttpResponse json(const JsonValue& j, int status_code = 200) {
        return {status_code, "application/json; charset=utf-8", j.dump()};
    }

    static HttpResponse html(std::string html_content, int status_code = 200) {
        return {status_code, "text/html; charset=utf-8", std::move(html_content)};
    }
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer() = default;
    ~HttpServer() { stop(); }

    void route(const std::string& method, const std::string& path, Handler handler);

    // Blocking serve loop bound to host (e.g. "0.0.0.0") and port
    void listen_and_serve(const std::string& host, uint16_t port);
    void stop();

private:
    struct RouteEntry {
        std::string method;
        std::string path;
        Handler handler;
    };
    std::vector<RouteEntry> routes_;
    std::atomic<bool> running_{false};
    int listen_fd_ = -1;

    void handle_client(int client_fd) const;
};

} // namespace chronokit
