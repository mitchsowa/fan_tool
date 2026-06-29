// Passive Modbus-RTU bus sniffer. Opens the serial port READ-ONLY (never
// transmits) and decodes every frame it sees, with CRC validation and
// inter-frame-gap framing. Use it to watch a third-party master (PLC) talk to
// the fan on a shared RS485 pair.
//
//   sniff <device> [baud] [parity N|E|O] [watch_addr]
//
// Defaults: 19200 E 11
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

static uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static const char* func_name(uint8_t f) {
    switch (f & 0x7F) {
        case 0x03: return "ReadHolding";
        case 0x04: return "ReadInput";
        case 0x06: return "WriteSingle";
        case 0x10: return "WriteMultiple";
        case 0x01: return "ReadCoils";
        case 0x02: return "ReadDiscrete";
        case 0x05: return "WriteCoil";
        default:   return "Func?";
    }
}

static uint16_t be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

static void decode(const std::vector<uint8_t>& f, double t0, int watch) {
    double t = now_ms() - t0;
    printf("[%9.1f ms] ", t);
    if (f.size() < 4) {  // too short to be a valid RTU frame
        printf("RUNT %zuB:", f.size());
        for (uint8_t b : f) printf(" %02X", b);
        printf("\n");
        return;
    }
    size_t n = f.size();
    uint16_t got = f[n - 2] | (f[n - 1] << 8);
    uint16_t calc = crc16(f.data(), n - 2);
    bool crc_ok = (got == calc);
    uint8_t addr = f[0];
    uint8_t func = f[1];

    printf("addr=%-3u %-13s %zuB", addr, func_name(func), n);
    if (func & 0x80) printf(" [EXCEPTION code=%u]", f.size() > 2 ? f[2] : 0);

    // Heuristic request vs response decode for the common functions.
    uint8_t base = func & 0x7F;
    if (!(func & 0x80)) {
        if ((base == 0x03 || base == 0x04) && n == 8) {
            printf("  REQ reg=%u qty=%u", be16(&f[2]), be16(&f[4]));
        } else if ((base == 0x03 || base == 0x04) && n >= 5 && f[2] == n - 5) {
            printf("  RSP bytes=%u  data:", f[2]);
            for (size_t i = 0; i + 1 < (size_t)f[2]; i += 2)
                printf(" %u", be16(&f[3 + i]));
        } else if (base == 0x06 && n == 8) {
            printf("  WRITE reg=%u val=%u", be16(&f[2]), be16(&f[4]));
        } else if (base == 0x10 && n == 8) {
            printf("  RSP-WRMULTI reg=%u qty=%u", be16(&f[2]), be16(&f[4]));
        } else if (base == 0x10 && n > 8) {
            printf("  REQ-WRMULTI reg=%u qty=%u", be16(&f[2]), be16(&f[4]));
        }
    }

    printf("  CRC=%s", crc_ok ? "OK" : "BAD");
    if (watch >= 0 && addr == (uint8_t)watch && !crc_ok)
        printf("  <-- addr matches but CRC BAD");
    printf("   raw:");
    for (uint8_t b : f) printf(" %02X", b);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char** argv) {
    const char* dev = argc > 1 ? argv[1] : "/dev/ttyUSB0";
    int baud = argc > 2 ? atoi(argv[2]) : 19200;
    char par = argc > 3 ? argv[3][0] : 'E';
    int watch = argc > 4 ? atoi(argv[4]) : 11;

    int fd = open(dev, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { fprintf(stderr, "open %s: %s\n", dev, strerror(errno)); return 1; }

    struct termios t;
    memset(&t, 0, sizeof(t));
    if (tcgetattr(fd, &t) != 0) { perror("tcgetattr"); return 1; }
    cfmakeraw(&t);
    speed_t sp;
    switch (baud) {
        case 9600: sp = B9600; break;
        case 19200: sp = B19200; break;
        case 38400: sp = B38400; break;
        case 57600: sp = B57600; break;
        case 115200: sp = B115200; break;
        default: sp = B19200; break;
    }
    cfsetispeed(&t, sp);
    cfsetospeed(&t, sp);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CSIZE; t.c_cflag |= CS8;
    t.c_cflag &= ~CSTOPB;            // 1 stop bit
    if (par == 'E' || par == 'e') { t.c_cflag |= PARENB; t.c_cflag &= ~PARODD; }
    else if (par == 'O' || par == 'o') { t.c_cflag |= PARENB | PARODD; }
    else { t.c_cflag &= ~PARENB; }
    t.c_iflag &= ~(INPCK | ISTRIP | IXON | IXOFF | IXANY);  // keep bytes verbatim
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) != 0) { perror("tcsetattr"); return 1; }

    // 3.5-char inter-frame gap. char ~= 11 bits.
    double char_ms = 11000.0 / baud;
    double gap_ms = char_ms * 3.5;
    if (gap_ms < 1.75) gap_ms = 1.75;

    fprintf(stderr,
            "Sniffing %s @ %d 8%c1  (frame gap %.2f ms)  watching addr %d\n"
            "READ-ONLY: this tool never transmits. Ctrl-C to stop.\n\n",
            dev, baud, par, gap_ms, watch);

    double t0 = now_ms();
    std::vector<uint8_t> frame;
    double last_byte = 0;
    unsigned long total_bytes = 0, total_frames = 0;

    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 1);  // 1 ms tick
        double t = now_ms();
        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[512];
            ssize_t r = read(fd, buf, sizeof(buf));
            if (r > 0) {
                if (!frame.empty() && (t - last_byte) > gap_ms) {
                    decode(frame, t0, watch); ++total_frames; frame.clear();
                }
                for (ssize_t i = 0; i < r; ++i) frame.push_back(buf[i]);
                total_bytes += r;
                last_byte = t;
            }
        } else {
            // idle tick: if a frame has been sitting past the gap, flush it
            if (!frame.empty() && (t - last_byte) > gap_ms) {
                decode(frame, t0, watch); ++total_frames; frame.clear();
            }
        }
    }
    return 0;
}
