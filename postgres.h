#ifndef POSTGRES_H
#define POSTGRES_H

#include <string>

class PostgreSQL {
public:
    PostgreSQL();

    bool connect(const std::string& connectionString);

    bool executeQuery(const std::string& query);

    void disconnect();

    bool isConnected() const;

private:
    bool connected;
};

#endif
