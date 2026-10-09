# MemOpt Setup Guide

## Requirements

- Linux
- C++17 compiler
- PostgreSQL
- libpqxx and PostgreSQL client libraries
- Git
- Linux `perf` where available

## Project Setup

Clone the project repository and enter the project directory.

```bash
git clone <repository-url>
cd MemOpt
```

## PostgreSQL

Create the database:

```sql
CREATE DATABASE memopt;
```

Run the project's schema from the `sql/schema.sql` file.

## Build

Compile the C++ backend using the project's source files and link PostgreSQL/libpqxx.

Example:

```bash
g++ -std=c++17 main.cpp src/aol/AOLCalculator.cpp src/profiling/MemoryProfiler.cpp src/tiering/HotnessPolicy.cpp src/tiering/AOLPolicy.cpp src/TierManager.cpp src/migration/MigrationController.cpp src/DatabaseManager.cpp -Iinclude -lpqxx -lpq -o memopt
```

Run:

```bash
./memopt
```

## Frontend

Open:

```text
frontend/index.html
```

in a browser.

## Important Prototype Note

The current frontend is designed around sample/prototype values. Later integration can connect the JavaScript dashboard to the C++ backend so that live profiling and database results are displayed.
