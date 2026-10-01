#!/usr/bin/env bash
set -euo pipefail

SERVER_BIN="$1"
CLIENT_BIN="$2"

PORT=$(python3 -c "import socket; s=socket.socket(); s.bind(('',0)); p=s.getsockname()[1]; s.close(); print(p)" 2>/dev/null || echo 50062)
DATA_DIR=$(mktemp -d)
SCRIPT=$(mktemp --suffix=.sql)
SERVER_PID=""

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
    [ -n "$SERVER_PID" ] && wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$DATA_DIR" "$SCRIPT"
}
trap cleanup EXIT

"$SERVER_BIN" "127.0.0.1:$PORT" "$DATA_DIR" &
SERVER_PID=$!

# Ждём пока сервер начнёт принимать соединения (до 5 секунд)
READY=0
for i in $(seq 1 25); do
    if bash -c "echo > /dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
        READY=1
        sleep 0.1
        break
    fi
    sleep 0.2
done

if [ "$READY" -eq 0 ]; then
    echo "FAIL: gRPC server did not start in time on port $PORT"
    exit 1
fi

cat > "$SCRIPT" <<'ENDSQL'
CREATE DATABASE smoke;
USE smoke;
CREATE TABLE items (id INT INDEXED, name STRING NOT NULL, price INT);
INSERT INTO items (id, name, price) VALUE
  (1, "apple", 50), (2, "banana", 30), (3, "cherry", 70);
SELECT * FROM items WHERE price >= 50;
UPDATE items SET price = 35 WHERE id == 2;
SELECT id, name, price FROM items WHERE id == 2;
DELETE FROM items WHERE price < 40;
SELECT COUNT(id) FROM items;
SELECT SUM(price) FROM items;
BAD SQL;
SELECT * FROM items;
DROP TABLE items;
DROP DATABASE smoke;
ENDSQL

OUTPUT=$("$CLIENT_BIN" "127.0.0.1:$PORT" "$SCRIPT" 2>&1) || true

fail() {
    echo "FAIL: $1"
    echo "=== output ==="
    echo "$OUTPUT"
    exit 1
}

# SELECT * WHERE price >= 50: apple(50) и cherry(70)
echo "$OUTPUT" | grep -qF '"apple"'       || fail "apple not found in initial SELECT"
echo "$OUTPUT" | grep -qF '"cherry"'      || fail "cherry not found in initial SELECT"

# SELECT WHERE id == 2 после UPDATE: banana с price=35
echo "$OUTPUT" | grep -qF '"banana"'      || fail "banana not found after UPDATE"
echo "$OUTPUT" | grep -qF '"price": 35'   || fail "price 35 not found after UPDATE"

# COUNT и SUM после DELETE (banana удалён): 2 строки, price 50+70=120
echo "$OUTPUT" | grep -qF '"COUNT(id)"'   || fail "COUNT aggregate key not found"
echo "$OUTPUT" | grep -qF '"SUM(price)"'  || fail "SUM aggregate key not found"
echo "$OUTPUT" | grep -qF '"2"'           || fail "COUNT result 2 not found"
echo "$OUTPUT" | grep -qF '"120"'         || fail "SUM result 120 not found"

# BAD SQL должен вернуть ошибку, но скрипт продолжается
echo "$OUTPUT" | grep -qF 'ERROR:'        || fail "ERROR not produced for bad SQL"

# После BAD SQL идут ещё OK-ответы (SELECT, DROP TABLE, DROP DATABASE)
OK_COUNT=$(echo "$OUTPUT" | grep -cF 'OK' || true)
[ "$OK_COUNT" -ge 7 ] || fail "expected at least 7 OK responses, got $OK_COUNT"

echo "PASS: gRPC smoke test"
exit 0
