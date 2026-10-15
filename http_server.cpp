#include "http_server.h"

#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalid = INVALID_SOCKET;
static void closeSock(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalid = -1;
static void closeSock(socket_t s) { close(s); }
#endif

#include "common.h"
#include "json.h"

namespace memopt {

std::string httpStatusText(int s) {
    switch (s) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

bool parseHttpRequestHead(const std::string& raw, HttpRequest& req, std::string& error) {
    size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) {
        error = "incomplete request head";
        return false;
    }
    std::istringstream in(raw.substr(0, end));
    std::string line;
    if (!std::getline(in, line)) {
        error = "empty request";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream rl(line);
    std::string target, version;
    rl >> req.method >> target >> version;
    if (req.method.empty() || target.empty() || version.rfind("HTTP/", 0) != 0) {
        error = "malformed request line";
        return false;
    }
    auto q = target.find('?');
    req.path = urlDecode(target.substr(0, q));
    req.query = q == std::string::npos ? "" : target.substr(q + 1);
    std::istringstream qs(req.query);
    std::string pair;
    while (std::getline(qs, pair, '&')) {
        if (pair.empty()) continue;
        auto eq = pair.find('=');
        req.params[urlDecode(pair.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
    }
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto c = line.find(':');
        if (c == std::string::npos) continue;
        req.headers[toLower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    req.body = raw.substr(end + 4);
    return true;
}

HttpServer::HttpServer(std::string host, int port, HttpHandler handler)
    : host_(std::move(host)), port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpServer::start(std::string& error) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) {
        error = "cannot create socket";
        return false;
    }
#ifndef _WIN32
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port_));
    std::string h = host_ == "localhost" ? "127.0.0.1" : host_;
    if (inet_pton(AF_INET, h.c_str(), &addr.sin_addr) != 1) {
        error = "invalid bind address '" + host_ + "' (use an IPv4 address such as 127.0.0.1)";
        closeSock(s);
        return false;
    }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        error = "cannot bind " + h + ":" + std::to_string(port_) +
                " - is another program (or another MemOpt) using this port? Try --port 8081";
        closeSock(s);
        return false;
    }
    if (listen(s, 16) != 0) {
        error = "listen() failed";
        closeSock(s);
        return false;
    }
    listen_fd_ = static_cast<long long>(s);
    running_ = true;
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ != -1) {
        closeSock(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
}

static bool sendAll(socket_t s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        ssize_t n = send(s, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

static void writeResponse(socket_t s, const HttpResponse& r) {
    std::ostringstream os;
    os << "HTTP/1.1 " << r.status << " " << httpStatusText(r.status) << "\r\n"
       << "Content-Type: " << r.content_type << "\r\n"
       << "Content-Length: " << r.body.size() << "\r\n"
       << "Connection: close\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n";
    for (const auto& kv : r.headers) os << kv.first << ": " << kv.second << "\r\n";
    os << "\r\n";
    sendAll(s, os.str() + r.body);
}

static HttpResponse errorResponse(int status, const std::string& msg) {
    HttpResponse r;
    r.status = status;
    r.body = "{\"ok\":false,\"error\":\"" + jsonEscape(msg) + "\"}";
    return r;
}

void HttpServer::handleClient(long long fd) {
    socket_t s = static_cast<socket_t>(fd);
#ifdef _WIN32
    DWORD tv = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv);
#else
    timeval tv{10, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    std::string raw;
    char buf[4096];
    size_t head_end = std::string::npos;
    while (head_end == std::string::npos) {
        int n = static_cast<int>(recv(s, buf, sizeof buf, 0));
        if (n <= 0) return;
        raw.append(buf, static_cast<size_t>(n));
        head_end = raw.find("\r\n\r\n");
        if (head_end == std::string::npos && raw.size() > kMaxHeaderBytes) {
            writeResponse(s, errorResponse(413, "request headers too large"));
            return;
        }
    }
    HttpRequest req;
    std::string err;
    if (!parseHttpRequestHead(raw, req, err)) {
        writeResponse(s, errorResponse(400, err));
        return;
    }
    size_t want = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        try {
            want = static_cast<size_t>(std::stoull(it->second));
        } catch (...) {
            writeResponse(s, errorResponse(400, "bad Content-Length"));
            return;
        }
    }
    if (want > kMaxBodyBytes) {
        writeResponse(s, errorResponse(413, "request body too large (max 64 KB)"));
        return;
    }
    while (req.body.size() < want) {
        int n = static_cast<int>(recv(s, buf, sizeof buf, 0));
        if (n <= 0) break;
        req.body.append(buf, static_cast<size_t>(n));
    }
    if (req.body.size() > want) req.body.resize(want);

    HttpResponse resp;
    try {
        resp = handler_(req);
    } catch (const std::exception& e) {
        resp = errorResponse(500, std::string("internal error: ") + e.what());
    }
    writeResponse(s, resp);
    std::cout << nowIso8601() << "  " << req.method << " " << req.path << " -> " << resp.status << std::endl;
}

void HttpServer::serveForever() {
    while (running_) {
        sockaddr_in client{};
#ifdef _WIN32
        int len = sizeof client;
#else
        socklen_t len = sizeof client;
#endif
        socket_t c = accept(static_cast<socket_t>(listen_fd_), reinterpret_cast<sockaddr*>(&client), &len);
        if (c == kInvalid) {
            if (!running_) break;
            continue;
        }
        handleClient(static_cast<long long>(c));
        closeSock(c);
    }
}

}  // namespace memopt
