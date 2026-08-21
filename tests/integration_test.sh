#!/usr/bin/env bash
# Интеграционный тест: гоняем реальный бинарник в режиме --stdio и сверяем
# байты ответа. Проверяется весь тракт целиком — сборка строки, эхо,
# обрамление ответа <CR><LF>, S-регистры, режимы +CMEE и составные команды.
set -u

BIN="${1:-./build/at-modem-server}"
DICT="${2:-config/modem.dict}"

if [[ ! -x "$BIN" ]]; then
    echo "не найден бинарник: $BIN" >&2
    echo "использование: $0 [путь-к-at-modem-server] [путь-к-словарю]" >&2
    exit 2
fi

failures=0

# check <название> <вход> <ожидаемый выход> [доп. аргументы бинарника]
check() {
    local name="$1" input="$2" want="$3"
    local extra="${4:-}"
    local got

    # shellcheck disable=SC2086
    got="$(printf '%b' "$input" | "$BIN" --stdio -f "$DICT" -q $extra 2>/dev/null | od -An -c | tr -s ' ')"
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

# ------------------------------------------------------------ базовый обмен
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

check 'backspace правит ввод'   'ATX\bI\r'    'ATX\b \bI\r\r\nNDM Systems\r\nNDM-LTE-1000\r\nRevision: 1.4.2-ndm\r\n\r\nOK\r\n'
check 'A/ повторяет команду'    'AT\rA/\r'    'AT\r\r\nOK\r\nA/\r\r\nOK\r\n'
check 'пробелы по краям'        '  AT  \r'    '  AT  \r\r\nOK\r\n'

# ------------------------------------------------------- режимы ошибок +CMEE
# По умолчанию +CMEE=0, как и предписывает 3GPP TS 27.007: краткое ERROR.
check 'CMEE=0: краткая ошибка'  'AT+CPIN=abcd\r' 'AT+CPIN=abcd\r\r\nERROR\r\n'

check 'CMEE=1: числовой код'    'AT+CMEE=1\rAT+CPIN=abcd\r' \
      'AT+CMEE=1\r\r\nOK\r\nAT+CPIN=abcd\r\r\n+CME ERROR: 16\r\n'

check 'CMEE=2: текст ошибки'    'AT+CMEE=2\rAT+CPIN=abcd\r' \
      'AT+CMEE=2\r\r\nOK\r\nAT+CPIN=abcd\r\r\n+CME ERROR: incorrect password\r\n'

check 'CMEE=2: неизвестная'     'AT+CMEE=2\rATXYZ\r' \
      'AT+CMEE=2\r\r\nOK\r\nATXYZ\r\r\n+CME ERROR: operation not supported\r\n'

check 'AT+CMEE? читает режим'   'AT+CMEE=2\rAT+CMEE?\r' \
      'AT+CMEE=2\r\r\nOK\r\nAT+CMEE?\r\r\n+CMEE: 2\r\n\r\nOK\r\n'

check 'ключ --cmee 2'           'AT+CPIN=abcd\r' \
      'AT+CPIN=abcd\r\r\n+CME ERROR: incorrect password\r\n' '--cmee 2'

# ------------------------------------------------------------- S-регистры
check 'ATS3? читает регистр'    'ATS3?\r'     'ATS3?\r\r\n013\r\n\r\nOK\r\n'
check 'ATS5=8 пишет регистр'    'ATS5=8\r'    'ATS5=8\r\r\nOK\r\n'
check 'ATS3=999 вне диапазона'  'ATS3=999\r'  'ATS3=999\r\r\nERROR\r\n'
check 'ATS3 без ? и = ошибка'   'ATS3\r'      'ATS3\r\r\nERROR\r\n'

# S3 задаёт символ конца строки: после ATS3=59 терминатором становится ';',
# и он же используется в обрамлении ответов.
check 'смена S3 меняет терминатор' 'ATS3=59\rAT;' \
      'ATS3=59\r;\nOK;\nAT;;\nOK;\n'

# ------------------------------------------------------- составные команды
check 'цепочка из двух команд'  'AT+CSQ;+CREG?\r' \
      'AT+CSQ;+CREG?\r\r\n+CSQ: 21,99\r\n\r\n+CREG: 0,1\r\n\r\nOK\r\n'

check 'цепочка с пустым блоком' 'AT;+CSQ;+CREG?\r' \
      'AT;+CSQ;+CREG?\r\r\n+CSQ: 21,99\r\n\r\n+CREG: 0,1\r\n\r\nOK\r\n'

check 'цепочка рвётся на ошибке' 'AT+CSQ;+NOPE\r' \
      'AT+CSQ;+NOPE\r\r\n+CSQ: 21,99\r\n\r\nERROR\r\n'

check 'цепочка с +CMEE=2'       'AT+CMEE=2\rAT+CSQ;+NOPE\r' \
      'AT+CMEE=2\r\r\nOK\r\nAT+CSQ;+NOPE\r\r\n+CSQ: 21,99\r\n\r\n+CME ERROR: operation not supported\r\n'

echo
if [[ $failures -eq 0 ]]; then
    echo "Интеграционные тесты пройдены."
else
    echo "Провалено проверок: $failures"
fi
exit $((failures == 0 ? 0 : 1))
