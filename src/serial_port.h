// SPDX-License-Identifier: MIT
#pragma once

#if !defined(__linux__) && !defined(__unix__) && !defined(__APPLE__)
#error "Сервер рассчитан на POSIX/Linux (termios)."
#endif

#include <termios.h>

#include <cstddef>
#include <string>

namespace ndm {

/// Обёртка над tty-устройством в «сыром» режиме.
///
/// Модем не должен получать от драйвера ни канонической построчной обработки,
/// ни собственного эха (эхо мы формируем сами по ATE), ни трансляции CR/LF —
/// иначе AT-протокол ломается. Исходные настройки терминала сохраняются
/// и восстанавливаются в деструкторе.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    /// Открывает tty (например /dev/ttyUSB0 или slave-конец pty от socat).
    void openDevice(const std::string& path, unsigned baud);

    /// Режим отладки: читаем stdin, пишем stdout. Если stdin — терминал,
    /// он тоже переводится в сырой режим.
    void openStdio();

    /// Читает доступные байты. Возвращает 0 при EOF, -1 если данных пока нет.
    long readSome(char* buf, std::size_t len);

    /// Пишет буфер целиком, корректно обрабатывая частичную запись и EINTR.
    void writeAll(const char* data, std::size_t len);
    void writeAll(const std::string& data) { writeAll(data.data(), data.size()); }

    /// Ждёт готовности к чтению не дольше timeoutMs. true — есть данные.
    bool waitReadable(int timeoutMs);

    int readFd() const noexcept { return readFd_; }
    const std::string& name() const noexcept { return name_; }

    void close();

    /// Преобразует скорость (9600, 115200, ...) в константу termios.
    /// Бросает std::runtime_error для неподдерживаемых значений.
    static speed_t toSpeed(unsigned baud);

private:
    void enterRawMode(int fd);

    int readFd_ = -1;
    int writeFd_ = -1;
    bool ownsFd_ = false;
    bool restore_ = false;
    int restoreFd_ = -1;
    termios saved_{};
    std::string name_;
};

}  // namespace ndm
