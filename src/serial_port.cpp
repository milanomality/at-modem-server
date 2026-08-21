// SPDX-License-Identifier: MIT
#include "serial_port.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "log.h"

namespace ndm {
namespace {

std::string errnoText(const std::string& what) {
    return what + ": " + std::strerror(errno);
}

}  // namespace

speed_t SerialPort::toSpeed(unsigned baud) {
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default:
        throw std::runtime_error("неподдерживаемая скорость порта: " + std::to_string(baud));
    }
}

SerialPort::~SerialPort() { close(); }

void SerialPort::enterRawMode(int fd) {
    if (tcgetattr(fd, &saved_) != 0) {
        throw std::runtime_error(errnoText("tcgetattr"));
    }
    restore_ = true;
    restoreFd_ = fd;

    termios tio = saved_;
    cfmakeraw(&tio);            // выключает ICANON, ECHO, обработку сигналов и трансляцию CR/LF
    tio.c_cflag |= (CLOCAL | CREAD);   // игнорируем модемные линии, разрешаем приём
    tio.c_cflag &= ~CRTSCTS;           // аппаратного управления потоком нет
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);  // программного тоже: XON/XOFF съел бы байты
    tio.c_cc[VMIN] = 1;                // блокирующее чтение как минимум одного байта
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        throw std::runtime_error(errnoText("tcsetattr"));
    }
}

void SerialPort::openDevice(const std::string& path, unsigned baud) {
    const speed_t speed = toSpeed(baud);

    // O_NONBLOCK на открытии: без него open() на настоящем порту ждёт DCD.
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error(errnoText("не удалось открыть " + path));
    }

    if (!isatty(fd)) {
        ::close(fd);
        throw std::runtime_error(path + " не является tty-устройством");
    }

    readFd_ = writeFd_ = fd;
    ownsFd_ = true;
    name_ = path;

    try {
        enterRawMode(fd);

        termios tio{};
        if (tcgetattr(fd, &tio) != 0) throw std::runtime_error(errnoText("tcgetattr"));
        if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0) {
            throw std::runtime_error(errnoText("cfsetspeed"));
        }
        tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
        tio.c_cflag |= CS8;              // 8 бит данных
        tio.c_cflag &= ~static_cast<tcflag_t>(PARENB);  // без чётности
        tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);  // один стоп-бит
        if (tcsetattr(fd, TCSANOW, &tio) != 0) throw std::runtime_error(errnoText("tcsetattr"));

        tcflush(fd, TCIOFLUSH);

        // Дальше работаем через poll(), блокирующий режим удобнее.
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            throw std::runtime_error(errnoText("fcntl"));
        }
    } catch (...) {
        close();
        throw;
    }

    logInfo("порт " + path + " открыт: " + std::to_string(baud) + " 8N1, raw");
}

void SerialPort::openStdio() {
    readFd_ = STDIN_FILENO;
    writeFd_ = STDOUT_FILENO;
    ownsFd_ = false;
    name_ = "<stdio>";

    if (isatty(readFd_)) {
        enterRawMode(readFd_);
        logInfo("режим stdio: терминал переведён в raw (выход — Ctrl+C)");
    } else {
        logInfo("режим stdio: ввод не терминал, termios не трогаем");
    }
}

bool SerialPort::waitReadable(int timeoutMs) {
    if (readFd_ < 0) return false;

    pollfd pfd{};
    pfd.fd = readFd_;
    pfd.events = POLLIN;

    const int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0) {
        if (errno == EINTR) return false;  // пришёл сигнал — вернёмся в цикл
        throw std::runtime_error(errnoText("poll"));
    }
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0;
}

long SerialPort::readSome(char* buf, std::size_t len) {
    for (;;) {
        const ssize_t n = ::read(readFd_, buf, len);
        if (n >= 0) return static_cast<long>(n);
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        throw std::runtime_error(errnoText("read"));
    }
}

void SerialPort::writeAll(const char* data, std::size_t len) {
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::write(writeFd_, data + done, len - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = writeFd_;
            pfd.events = POLLOUT;
            ::poll(&pfd, 1, 100);
            continue;
        }
        throw std::runtime_error(errnoText("write"));
    }
}

void SerialPort::close() {
    if (restore_ && restoreFd_ >= 0) {
        tcsetattr(restoreFd_, TCSANOW, &saved_);
        restore_ = false;
    }
    if (ownsFd_ && readFd_ >= 0) {
        ::close(readFd_);
    }
    readFd_ = writeFd_ = -1;
    ownsFd_ = false;
}

}  // namespace ndm
