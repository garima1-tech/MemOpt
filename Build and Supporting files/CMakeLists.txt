cmake_minimum_required(VERSION 3.16)
project(MemOpt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ------------------------------------------------------------------ libpq ---
# Windows: pass -DPostgreSQL_ROOT="C:/Program Files/PostgreSQL/16"
# Linux:   install libpq-dev (Debian/Ubuntu) or libpq-devel (Fedora).
set(PostgreSQL_ROOT "" CACHE PATH "PostgreSQL install folder (contains include/ and lib/)")
set(_pq_hints)
if(PostgreSQL_ROOT)
  list(APPEND _pq_hints "${PostgreSQL_ROOT}")
endif()
if(WIN32)
  file(GLOB _pg_win_dirs "C:/Program Files/PostgreSQL/*")
  list(SORT _pg_win_dirs ORDER DESCENDING)
  list(APPEND _pq_hints ${_pg_win_dirs})
endif()

find_path(LIBPQ_INCLUDE_DIR libpq-fe.h
  HINTS ${_pq_hints}
  PATH_SUFFIXES include include/postgresql postgresql pgsql)
find_library(LIBPQ_LIBRARY NAMES pq libpq
  HINTS ${_pq_hints}
  PATH_SUFFIXES lib lib64)

if(NOT LIBPQ_INCLUDE_DIR OR NOT LIBPQ_LIBRARY)
  message(FATAL_ERROR
    "libpq (PostgreSQL client library) not found.\n"
    "  Windows: install PostgreSQL 14+ and configure with\n"
    "           -DPostgreSQL_ROOT=\"C:/Program Files/PostgreSQL/16\"\n"
    "  Ubuntu:  sudo apt install libpq-dev\n"
    "  Or set LIBPQ_INCLUDE_DIR and LIBPQ_LIBRARY by hand.")
endif()
message(STATUS "libpq include: ${LIBPQ_INCLUDE_DIR}")
message(STATUS "libpq library: ${LIBPQ_LIBRARY}")

# --------------------------------------------------------- core library ---
add_library(memopt_core STATIC
  src/common/common.cpp
  src/common/config.cpp
  src/common/json.cpp
  src/dbms/postgres.cpp
  src/dbms/dataset.cpp
  src/dbms/workload.cpp
  src/profiling/profiler.cpp
  src/profiling/unit_metrics.cpp
  src/aol/aol.cpp
  src/hotness/hotness.cpp
  src/soar/soar.cpp
  src/alto/alto.cpp
  src/tiering/tier_manager.cpp
  src/benchmark/benchmark.cpp
  src/results/result_logger.cpp
  src/api/http_server.cpp
  src/api/app.cpp
  src/api/api.cpp
)
target_include_directories(memopt_core PUBLIC include ${LIBPQ_INCLUDE_DIR})
target_link_libraries(memopt_core PUBLIC ${LIBPQ_LIBRARY})
if(WIN32)
  target_link_libraries(memopt_core PUBLIC ws2_32)
  target_compile_definitions(memopt_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
if(MSVC)
  target_compile_options(memopt_core PUBLIC /W4 /utf-8 /EHsc)
else()
  target_compile_options(memopt_core PUBLIC -Wall -Wextra)
endif()

# ------------------------------------------------------------ executable ---
add_executable(memopt src/main.cpp)
target_link_libraries(memopt PRIVATE memopt_core)

# ----------------------------------------------------------------- tests ---
# Tests are plain executables (no GoogleTest download needed).
# Exit code 77 = SKIPPED (used when PostgreSQL is not reachable), so a
# missing database is reported as "Skipped", never as "Passed".
enable_testing()
set(MEMOPT_UNIT_TESTS
  test_aol test_hotness_soar test_alto test_tier_manager test_csv_json test_api_validation
  test_db_connection_failure)
set(MEMOPT_DB_TESTS test_db_workload test_end_to_end)

foreach(t ${MEMOPT_UNIT_TESTS} ${MEMOPT_DB_TESTS})
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE memopt_core)
  add_test(NAME ${t} COMMAND ${t})
  set_tests_properties(${t} PROPERTIES
    ENVIRONMENT "MEMOPT_ROOT=${CMAKE_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endforeach()
foreach(t ${MEMOPT_DB_TESTS})
  set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS "database")
endforeach()
