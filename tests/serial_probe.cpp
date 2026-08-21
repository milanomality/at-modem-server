// SPDX-License-Identifier: MIT
//
// Тестовый клиент для проверки сервера через настоящее tty-устройство.
//
// Открывает порт, отправляет одну команду и печатает в stdout всё, что
// пришло в ответ, — байт в байт, без какой-либо интерпретации. Сравнением
// занимается вызывающий скрипт.
//
// Намеренно не использует src/serial_port.* и вообще ничего из проекта:
// если в работе с termios на стороне сервера есть ошибка, клиент на том же
// коде мог бы её повторить и тем самым замаскировать.
//
//   serial_probe <устройство> <скорость> <команда-с-escape>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr int kPollStepMs = 20;   ///< шаг ожидания
constexpr int kIdleMs = 400;      ///< тишина, после которой считаем ответ полным
constexpr int kTotalMs = 5000;    ///< общий предел ожидания

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 == in.size()) {
            out += in[i];
            continue;
        }
        switch (in[++i]) {
        case 'r':  out += '\r';   break;
        case 'n':  out += '\n';   break;
        case 't':  out += '\t';   break;
        case 'b':  out += '\b';   break;
        case 'e':  out += '\x1b'; break;
        case '\\': out += '\\';   break;
        default:
            out += '\\';
            out += in[i];
            break;
        }
    }
    return out;
}

speed_t toSpeed(unsigned baud) {
    switch (baud) {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default:     return B0;
    }
}

bool configure(int fd, unsigned baud) {
    const speed_t speed = toSpeed(baud);
    if (speed == B0) {
        std::fprintf(stderr, "serial_probe: неподдерживаемая скорость %u\n", baud);
        return false;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        std::perror("serial_probe: tcgetattr");
        return false;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB);
    tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0) {
        std::perror("serial_probe: cfsetspeed");
        return false;
    }
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        std::perror("serial_probe: tcsetattr");
        return false;
    }

    // Выкидываем всё, что осталось в очередях от предыдущих запусков.
    tcflush(fd, TCIOFLUSH);
    return true;
}

bool writeAll(int fd, const std::string& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        std::perror("serial_probe: write");
        return false;
    }
    return true;
}

/// Читает ответ, пока он не перестанет приходить, и печатает его в stdout.
void drain(int fd) {
    char buf[512];
    int elapsed = 0;
    int idle = 0;
    bool gotAnything = false;

    while (elapsed < kTotalMs) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        const int rc = ::poll(&pfd, 1, kPollStepMs);
        elapsed += kPollStepMs;

        if (rc > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                std::fwrite(buf, 1, static_cast<std::size_t>(n), stdout);
                gotAnything = true;
                idle = 0;
                continue;
            }
        }

        idle += kPollStepMs;
        // Пока не пришло вообще ничего, ждём до общего предела: сервер мог
        // ещё не успеть открыть порт.
        if (gotAnything && idle >= kIdleMs) break;
    }

    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "использование: %s <устройство> <скорость> <команда>\n", argv[0]);
        return 2;
    }

    const char* device = argv[1];
    const auto baud = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    const std::string payload = unescape(argv[3]);

    const int fd = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "serial_probe: не удалось открыть %s: %s\n", device, std::strerror(errno));
        return 1;
    }
    if (!isatty(fd)) {
        std::fprintf(stderr, "serial_probe: %s не является tty\n", device);
        ::close(fd);
        return 1;
    }

    int status = 0;
    if (!configure(fd, baud) || !writeAll(fd, payload)) {
        status = 1;
    } else {
        drain(fd);
    }

    ::close(fd);
    return status;
}
