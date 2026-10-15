// http_server.h - Minimal single-threaded HTTP/1.1 server (Winsock / POSIX).
//
// Small on purpose: one request per connection, Content-Length bodies only,
// 64 KB body limit, binds to 127.0.0.1 by default. Good enough for a local
// demo dashboard; not meant for the public internet.
#pragma once

#include <functional>
#include <map>
#include <string>

namespace memopt {

struct HttpRequest {
    std::string method;
    std::string path;    // without query string
    std::string query;   // raw query string (after '?')
    std::map<std::string, std::string> headers;  // lower-case keys
    std::map<std::string, std::string> params;   // decoded query parameters
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

std::string httpStatusText(int status);
std::string urlDecode(const std::string& s);

// Parses the request line + headers (+ body if present in `raw`).
// Exposed for unit tests.
bool parseHttpRequestHead(const std::string& raw, HttpRequest& req, std::string& error);

class HttpServer {
public:
    HttpServer(std::string host, int port, HttpHandler handler);
    ~HttpServer();

    bool start(std::string& error);  // bind + listen
    void serveForever();             // blocks; returns after stop()
    void stop();

    static constexpr size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr size_t kMaxBodyBytes = 64 * 1024;

private:
    std::string host_;
    int port_;
    HttpHandler handler_;
    long long listen_fd_ = -1;  // SOCKET on Windows, int on POSIX
    bool running_ = false;

    void handleClient(long long fd);
};

}  // namespace memopt
