// Tests: API routing and request validation, without needing PostgreSQL.
// The database is deliberately pointed at an unused port so every
// database-dependent endpoint must answer 503 (never a fake success).
#include "api.h"
#include "test_util.h"

using namespace memopt;

static AppState& state() {
    static AppState* s = [] {
        AppConfig c = AppConfig::load();
        c.db_host = "127.0.0.1";
        c.db_port = 1;  // nothing listens here
        c.db_password = "DoNotLeakThis";
        c.db_connect_timeout_s = 2;
        return new AppState(c);
    }();
    return *s;
}

static HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    HttpRequest r;
    r.method = method;
    auto q = path.find('?');
    r.path = path.substr(0, q);
    if (q != std::string::npos) {
        std::string raw = method + " " + path + " HTTP/1.1\r\n\r\n";
        std::string err;
        parseHttpRequestHead(raw, r, err);
    }
    r.body = body;
    Api api(state());
    return api.handle(r);
}

TEST(health_ok) {
    auto r = call("GET", "/api/health");
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"ok\":true") != std::string::npos);
}

TEST(unknown_endpoint_404) { CHECK_EQ(call("GET", "/api/nope").status, 404); }

TEST(wrong_method_405) {
    CHECK_EQ(call("GET", "/api/workload/run").status, 405);
    CHECK_EQ(call("POST", "/api/health").status, 405);
    CHECK_EQ(call("DELETE", "/api/placement").status, 405);
}

TEST(malformed_json_400) {
    CHECK_EQ(call("POST", "/api/workload/run", "{bad json").status, 400);
    CHECK_EQ(call("POST", "/api/workload/run", "[1,2]").status, 400);
}

TEST(field_validation_400) {
    CHECK_EQ(call("POST", "/api/workload/run", R"({"workload":"drop table"})").status, 400);
    CHECK_EQ(call("POST", "/api/workload/run", R"({"workload":42})").status, 400);
    CHECK_EQ(call("POST", "/api/workload/run", R"({"repetitions":0})").status, 400);
    CHECK_EQ(call("POST", "/api/workload/run", R"({"repetitions":1000})").status, 400);
    CHECK_EQ(call("POST", "/api/workload/run", R"({"repetitions":2.5})").status, 400);
    CHECK_EQ(call("POST", "/api/benchmark/run", R"({"epochs":1})").status, 400);
    CHECK_EQ(call("POST", "/api/placement/simulate", R"({"dry_run":"yes"})").status, 400);
}

TEST(database_endpoints_503_when_down) {
    CHECK_EQ(call("POST", "/api/workload/run", R"({"workload":"random","repetitions":1})").status, 503);
    CHECK_EQ(call("POST", "/api/database/test").status, 503);
    CHECK_EQ(call("GET", "/api/database/tables").status, 503);
    CHECK_EQ(call("POST", "/api/benchmark/run", "{}").status, 503);
    CHECK_EQ(call("POST", "/api/profiling/run", "{}").status, 503);
}

TEST(status_reports_down_without_password) {
    auto r = call("GET", "/api/database/status");
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"connected\":false") != std::string::npos);
    CHECK(r.body.find("DoNotLeakThis") == std::string::npos);
    CHECK(r.body.find("\"password_set\":true") != std::string::npos);
    auto t = call("POST", "/api/database/test");
    CHECK(t.body.find("DoNotLeakThis") == std::string::npos);
}

TEST(prerequisites_409_or_404) {
    CHECK_EQ(call("POST", "/api/placement/simulate", "{}").status, 409);  // no workload yet
    CHECK_EQ(call("GET", "/api/workload/latest").status, 404);
    CHECK_EQ(call("GET", "/api/benchmark/latest").status, 404);
}

TEST(placement_get_works_without_data) {
    auto r = call("GET", "/api/placement");
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("\"has_workload_data\":false") != std::string::npos);
}

TEST(export_type_validation) {
    CHECK_EQ(call("GET", "/api/results/export?type=passwords").status, 400);
}

TEST(static_files_and_traversal) {
    auto idx = call("GET", "/");
    CHECK_EQ(idx.status, 200);
    CHECK(idx.content_type.find("text/html") != std::string::npos);
    CHECK_EQ(call("GET", "/script.js").status, 200);
    CHECK_EQ(call("GET", "/../CMakeLists.txt").status, 404);
    CHECK_EQ(call("GET", "/..%2fCMakeLists.txt").status, 404);
    CHECK_EQ(call("GET", "/sub/dir.js").status, 404);
    CHECK_EQ(call("POST", "/index.html").status, 405);
}

TEST(http_head_parser) {
    HttpRequest r;
    std::string err;
    CHECK(parseHttpRequestHead("POST /api/x?type=a%20b&n=1 HTTP/1.1\r\nContent-Length: 2\r\nX-Y: z\r\n\r\n{}", r, err));
    CHECK_EQ(r.method, std::string("POST"));
    CHECK_EQ(r.path, std::string("/api/x"));
    CHECK_EQ(r.params["type"], std::string("a b"));
    CHECK_EQ(r.headers["content-length"], std::string("2"));
    CHECK_EQ(r.body, std::string("{}"));
    HttpRequest bad;
    CHECK(!parseHttpRequestHead("garbage\r\n\r\n", bad, err));
}

int main() { return RUN_ALL(); }
