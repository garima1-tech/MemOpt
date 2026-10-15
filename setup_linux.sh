#!/usr/bin/env bash
# Creates (or resets) the MemOpt demo database on Linux / WSL2 and loads data.
#
#   ./scripts/setup_linux.sh            # uses .env or defaults
#   MEMOPT_DB_PASSWORD=secret ./scripts/setup_linux.sh
#
# Prerequisites (Ubuntu/Debian/WSL2):
#   sudo apt install postgresql postgresql-client libpq-dev cmake g++
#   sudo service postgresql start
#   sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'choose-a-password';"
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Load .env without executing it.
if [[ -f .env ]]; then
  while IFS='=' read -r key value; do
    [[ -z "$key" || "$key" =~ ^[[:space:]]*# ]] && continue
    key="$(echo "$key" | xargs)"; value="$(echo "$value" | xargs)"
    value="${value%\"}"; value="${value#\"}"
    [[ -z "${!key:-}" ]] && export "$key=$value"
  done < .env
fi

HOST="${MEMOPT_DB_HOST:-localhost}"
PORT="${MEMOPT_DB_PORT:-5432}"
USER_NAME="${MEMOPT_DB_USER:-postgres}"
DB="${MEMOPT_DB_NAME:-memopt_demo}"
[[ "$DB" =~ ^[A-Za-z_][A-Za-z0-9_]{0,62}$ ]] || { echo "ERROR: invalid database name '$DB'"; exit 1; }

command -v psql >/dev/null || { echo "ERROR: psql not found. sudo apt install postgresql-client"; exit 1; }

if [[ -z "${MEMOPT_DB_PASSWORD:-}" ]]; then
  read -r -s -p "Password for PostgreSQL user '$USER_NAME': " MEMOPT_DB_PASSWORD; echo
fi
export PGPASSWORD="$MEMOPT_DB_PASSWORD"
PSQL=(psql -h "$HOST" -p "$PORT" -U "$USER_NAME" -v ON_ERROR_STOP=1 -q)

if ! "${PSQL[@]}" -d postgres -tAc "SELECT 1" >/dev/null; then
  echo "ERROR: cannot connect to PostgreSQL at $HOST:$PORT as $USER_NAME."
  echo "       Is it running?  sudo service postgresql start"
  exit 1
fi

if [[ "$("${PSQL[@]}" -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$DB'")" != "1" ]]; then
  echo "Creating database $DB"
  "${PSQL[@]}" -d postgres -c "CREATE DATABASE \"$DB\" ENCODING 'UTF8'"
else
  echo "Database $DB exists - it will be RESET"
fi

echo "Loading workloads/schema.sql"
"${PSQL[@]}" -d "$DB" -f workloads/schema.sql
echo "Loading workloads/sample_data.sql"
"${PSQL[@]}" -d "$DB" -f workloads/sample_data.sql
unset PGPASSWORD

if [[ ! -f .env ]]; then
  sed -e "s|^MEMOPT_DB_HOST=.*|MEMOPT_DB_HOST=$HOST|" \
      -e "s|^MEMOPT_DB_PORT=.*|MEMOPT_DB_PORT=$PORT|" \
      -e "s|^MEMOPT_DB_NAME=.*|MEMOPT_DB_NAME=$DB|" \
      -e "s|^MEMOPT_DB_USER=.*|MEMOPT_DB_USER=$USER_NAME|" \
      -e "s|^MEMOPT_DB_PASSWORD=.*|MEMOPT_DB_PASSWORD=$MEMOPT_DB_PASSWORD|" .env.example > .env
  chmod 600 .env
  echo ".env created (git-ignored)"
fi
echo "Database ready. Next: ./scripts/build_linux.sh && ./build/memopt serve"
