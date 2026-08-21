#!/usr/bin/env bash
# Сквозной тест через НАСТОЯЩЕЕ tty-устройство.
#
# Поднимает виртуальную пару pty через socat, запускает сервер на одном конце
# с ключом --device и стучится во второй конец отдельным клиентом
# (tests/serial_probe.cpp). В отличие от integration_test.sh, здесь реально
# исполняется весь serial-путь: open(), termios, cfmakeraw, скорость порта,
# poll() на дескрипторе устройства.
#
# Если socat не установлен, тест сообщает о пропуске кодом 77 — ctest
# помечает его как skipped, а не как провал.
set -u

BIN="${1:-./build/at-modem-server}"
PROBE="${2:-./build/serial_probe}"
DICT="${3:-config/modem.dict}"
BAUD=115200

for f in "$BIN" "$PROBE"; do
    if [[ ! -x "$f" ]]; then
        echo "не найден бинарник: $f" >&2
        echo "использование: $0 [at-modem-server] [serial_probe] [словарь]" >&2
        exit 2
    fi
done

if ! command -v socat >/dev/null; then
    echo "socat не установлен — сквозной tty-тест пропущен (sudo apt install socat)"
    exit 77
fi

SERVER_LINK="$(mktemp -u /tmp/ttyModem.XXXXXX)"
CLIENT_LINK="$(mktemp -u /tmp/ttyClient.XXXXXX)"
SERVER_LOG="$(mktemp /tmp/at-modem-server.XXXXXX.log)"
SOCAT_PID=""
SERVER_PID=""

cleanup() {
    # Закрываем удерживающий дескриптор до того, как гасим socat.
    exec 3>&- 2>/dev/null || true
    [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null
    [[ -n "$SOCAT_PID"  ]] && kill "$SOCAT_PID"  2>/dev/null
    wait "$SERVER_PID" "$SOCAT_PID" 2>/dev/null
    rm -f "$SERVER_LINK" "$CLIENT_LINK" "$SERVER_LOG"
}
trap cleanup EXIT

socat "pty,raw,echo=0,link=$SERVER_LINK" "pty,raw,echo=0,link=$CLIENT_LINK" >/dev/null 2>&1 &
SOCAT_PID=$!

for _ in $(seq 100); do
    [[ -e "$SERVER_LINK" && -e "$CLIENT_LINK" ]] && break
    sleep 0.05
done
if [[ ! -e "$SERVER_LINK" || ! -e "$CLIENT_LINK" ]]; then
    echo "socat не создал пару pty" >&2
    exit 1
fi

# Держим клиентский конец открытым на всё время прогона. Клиент открывает и
# закрывает порт на каждую команду, а socat завершился бы, увидев, что
# последний потребитель pty отвалился.
exec 3<>"$CLIENT_LINK"

"$BIN" --device "$SERVER_LINK" --baud "$BAUD" --dict "$DICT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

for _ in $(seq 100); do
    grep -q "открыт" "$SERVER_LOG" 2>/dev/null && break
    kill -0 "$SERVER_PID" 2>/dev/null || break
    sleep 0.05
done
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "сервер не запустился, лог:" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi

failures=0

# check <название> <команда> <ожидаемый ответ>
check() {
    local name="$1" input="$2" want="$3"
    local got

    got="$("$PROBE" "$CLIENT_LINK" "$BAUD" "$input" | od -An -c | tr -s ' ')"
    want="$(printf '%b' "$want" | od -An -c | tr -s ' ')"

    if [[ "$got" == "$want" ]]; then
        printf '[  OK  ] %s\n' "$name"
    else
        printf '[ FAIL ] %s\n' "$name"
        printf '         ожидалось:%s\n' "$want"
        printf '         получено: %s\n' "$got"
        failures=$((failures + 1))
    fi
}

echo "сервер на $SERVER_LINK, клиент на $CLIENT_LINK, $BAUD 8N1"
echo

check 'AT через tty'            'AT\r'          'AT\r\r\nOK\r\n'
check 'ATI многострочный'       'ATI\r' \
      'ATI\r\r\nNDM Systems\r\nNDM-LTE-1000\r\nRevision: 1.4.2-ndm\r\n\r\nOK\r\n'
check 'AT+COPS? с кавычками'    'AT+COPS?\r' \
      'AT+COPS?\r\r\n+COPS: 0,0,"MegaFon",7\r\n\r\nOK\r\n'
check 'AT+CPIN=1234'            'AT+CPIN=1234\r' 'AT+CPIN=1234\r\r\nOK\r\n'
check 'нераспознанная -> ERROR' 'ATXYZ\r'        'ATXYZ\r\r\nERROR\r\n'
check 'составная команда'       'AT+CSQ;+CREG?\r' \
      'AT+CSQ;+CREG?\r\r\n+CSQ: 21,99\r\n\r\n+CREG: 0,1\r\n\r\nOK\r\n'
check 'S-регистр ATS3?'         'ATS3?\r'        'ATS3?\r\r\n013\r\n\r\nOK\r\n'

# Состояние обязано пережить закрытие и повторное открытие порта клиентом:
# модем не сбрасывается оттого, что терминал отсоединился.
check 'ATE0 гасит эхо'          'ATE0\r'         'ATE0\r\r\nOK\r\n'
check 'эхо выключено и после переоткрытия порта' 'AT\r' '\r\nOK\r\n'
check 'ATE1 не отражается'      'ATE1\r'         '\r\nOK\r\n'
check 'эхо снова работает'      'AT\r'           'AT\r\r\nOK\r\n'

echo
if [[ $failures -eq 0 ]]; then
    echo "Сквозной tty-тест пройден."
else
    echo "Провалено проверок: $failures"
    echo "--- лог сервера ---"
    cat "$SERVER_LOG"
fi
exit $((failures == 0 ? 0 : 1))
