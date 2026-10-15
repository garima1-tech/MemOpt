// api.h - REST API used by the dashboard (see docs/api.md).
//
// All endpoints return JSON. Errors use {"ok":false,"error":"..."} with a
// proper status code (400 bad input, 404 unknown, 405 wrong method,
// 409 missing prerequisite, 503 database unavailable).
// There is NO endpoint that executes arbitrary SQL.
#pragma once

#include <string>

#include "app.h"
#include "http_server.h"
#include "json.h"

namespace memopt {

class Api {
public:
    explicit Api(AppState& state) : s_(state) {}

    // Routes /api/* and static dashboard files.
    HttpResponse handle(const HttpRequest& req);

    // JSON helpers (also used by the CLI).
    static JsonValue metricJson(const Metric& m);
    static JsonValue workloadJson(const WorkloadResult& r);
    static JsonValue decisionJson(const PlacementDecision& d);
    static JsonValue migrationJson(const MigrationRecord& m);
    static JsonValue profileJson(const ProfileReport& p);
    static JsonValue benchmarkJson(const BenchmarkResult& b);

private:
    AppState& s_;

    HttpResponse health();
    HttpResponse databaseStatus();
    HttpResponse databaseTest();
    HttpResponse databaseTables();
    HttpResponse workloadRun(const JsonValue& body);
    HttpResponse workloadLatest();
    HttpResponse profilingRun(const JsonValue& body);
    HttpResponse profilingLatest();
    HttpResponse placementGet();
    HttpResponse placementSimulate(const JsonValue& body);
    HttpResponse migrations();
    HttpResponse benchmarkRun(const JsonValue& body);
    HttpResponse benchmarkLatest();
    HttpResponse resultsExport(const HttpRequest& req);
    HttpResponse tiersReset();
    HttpResponse config();
    HttpResponse serveStatic(const HttpRequest& req);
};

HttpResponse jsonResponse(int status, const JsonValue& v);
HttpResponse jsonError(int status, const std::string& message);

}  // namespace memopt
