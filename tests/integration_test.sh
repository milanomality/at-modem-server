#!/usr/bin/env bash
# Интеграционный тест: гоняем реальный бинарник в режиме --stdio и сверяем
# байты ответа. Проверяется весь тракт целиком — сборка строки, эхо,
# обрамление ответа <CR><LF> и обработка нераспознанных команд.
set -u

BIN="${1:-./build/at-modem-server}"
DICT="${2:-config/modem.dict}"

if [[ ! -x "$BIN" ]]; then
    echo "не найден бинарник: $BIN" >&2
    echo "использование: $0 [путь-к-at-modem-server] [путь-к-словарю]" >&2
    exit 2
fi

failures=0

# check <название> <вход> <ожидаемый выход>
check() {
    local name="$1" input="$2" want="$3"
    local got

    got="$(printf '%b' "$input" | "$BIN" --stdio -f "$DICT" -q 2>/dev/null | od -An -c | tr -s ' ')"
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

# Эхо включено по умолчанию (ATE1), поэтому в выводе сначала повтор команды.
check 'AT -> OK'                'AT\r'        'AT\r\r\nOK\r\n'
check 'регистр не важен'        'at\r'        'at\r\r\nOK\r\n'
check 'неизвестная -> ERROR'    'ATXYZ\r'     'ATXYZ\r\r\nERROR\r\n'
check 'пустая строка молчит'    '\r'          '\r'
check 'CRLF не двоит ответ'     'AT\r\n'      'AT\r\r\nOK\r\n'

check 'ATE0 выключает эхо'      'ATE0\rAT\r'  'ATE0\r\r\nOK\r\n\r\nOK\r\n'
check 'ATE1 включает обратно'   'ATE0\rATE1\rAT\r' \
                                'ATE0\r\r\nOK\r\n\r\nOK\r\nAT\r\r\nOK\r\n'

check 'ATI многострочный'       'ATI\r' \
      'ATI\r\r\nNDM Systems\r\nNDM-LTE-1000\r\nRevision: 1.4.2-ndm\r\n\r\nOK\r\n'

check 'AT+COPS? из словаря'     'AT+COPS?\r' \
      'AT+COPS?\r\r\n+COPS: 0,0,"MegaFon",7\r\n\r\nOK\r\n'

check 'AT+COPS=0,0 по шаблону'  'AT+COPS=0,0\r' 'AT+COPS=0,0\r\r\nOK\r\n'
check 'AT+CPIN? готов'          'AT+CPIN?\r'    'AT+CPIN?\r\r\n+CPIN: READY\r\n\r\nOK\r\n'
check 'AT+CPIN=1234 принят'     'AT+CPIN=1234\r' 'AT+CPIN=1234\r\r\nOK\r\n'
check 'AT+CPIN=abcd отвергнут'  'AT+CPIN=abcd\r' \
      'AT+CPIN=abcd\r\r\n+CME ERROR: incorrect password\r\n'

check 'backspace правит ввод'   'ATX\bI\r'    'ATX\b \bI\r\r\nNDM Systems\r\nNDM-LTE-1000\r\nRevision: 1.4.2-ndm\r\n\r\nOK\r\n'
check 'A/ повторяет команду'    'AT\rA/\r'    'AT\r\r\nOK\r\nA/\r\r\nOK\r\n'
check 'пробелы по краям'        '  AT  \r'    '  AT  \r\r\nOK\r\n'

echo
if [[ $failures -eq 0 ]]; then
    echo "Интеграционные тесты пройдены."
else
    echo "Провалено проверок: $failures"
fi
exit $((failures == 0 ? 0 : 1))
