#!/usr/bin/env bash
# Поднимает виртуальную пару pty через socat и запускает на одном её конце
# сервер. Второй конец остаётся клиенту — minicom, picocom, screen или cat.
#
#   ./scripts/run_socat.sh                 # словарь и бинарник по умолчанию
#   ./scripts/run_socat.sh ./build/at-modem-server config/modem.csv
#
# В другом терминале:
#   picocom -b 115200 --omap crcrlf /tmp/ttyClient
#   minicom -D /tmp/ttyClient -b 115200
set -euo pipefail

BIN="${1:-./build/at-modem-server}"
DICT="${2:-config/modem.dict}"
LINK_SERVER="${LINK_SERVER:-/tmp/ttyModem}"
LINK_CLIENT="${LINK_CLIENT:-/tmp/ttyClient}"

command -v socat >/dev/null || { echo "нужен socat: sudo apt install socat" >&2; exit 2; }
[[ -x "$BIN" ]] || { echo "не найден бинарник: $BIN (соберите проект)" >&2; exit 2; }

rm -f "$LINK_SERVER" "$LINK_CLIENT"

socat -d -d \
    "pty,raw,echo=0,link=$LINK_SERVER" \
    "pty,raw,echo=0,link=$LINK_CLIENT" &
SOCAT_PID=$!
trap 'kill "$SOCAT_PID" 2>/dev/null || true; rm -f "$LINK_SERVER" "$LINK_CLIENT"' EXIT

# Ссылки появляются асинхронно — ждём, но не вечно.
for _ in $(seq 50); do
    [[ -e "$LINK_SERVER" && -e "$LINK_CLIENT" ]] && break
    sleep 0.1
done
[[ -e "$LINK_SERVER" ]] || { echo "socat не создал $LINK_SERVER" >&2; exit 1; }

echo
echo "Клиентский конец: $LINK_CLIENT"
echo "Подключайтесь:    picocom -b 115200 --omap crcrlf $LINK_CLIENT"
echo

exec "$BIN" --device "$LINK_SERVER" --dict "$DICT" --verbose
