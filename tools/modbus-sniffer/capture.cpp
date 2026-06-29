// Raw serial capture: open port at given baud/parity (8 data, 1 stop), dump
// every received byte to stdout verbatim. READ-ONLY, never transmits.
//   capture <device> [baud] [parity N|E|O] > raw.bin
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* dev = argc > 1 ? argv[1] : "/dev/ttyUSB0";
    int baud = argc > 2 ? atoi(argv[2]) : 19200;
    char par = argc > 3 ? argv[3][0] : 'E';
    int fd = open(dev, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }
    struct termios t; memset(&t, 0, sizeof(t));
    tcgetattr(fd, &t); cfmakeraw(&t);
    speed_t sp = B19200;
    switch (baud) { case 9600: sp=B9600; break; case 19200: sp=B19200; break;
        case 38400: sp=B38400; break; case 57600: sp=B57600; break;
        case 115200: sp=B115200; break; }
    cfsetispeed(&t, sp); cfsetospeed(&t, sp);
    t.c_cflag |= (CLOCAL | CREAD); t.c_cflag &= ~CSIZE; t.c_cflag |= CS8;
    t.c_cflag &= ~CSTOPB;
    if (par=='E'||par=='e'){ t.c_cflag|=PARENB; t.c_cflag&=~PARODD; }
    else if (par=='O'||par=='o'){ t.c_cflag|=PARENB|PARODD; }
    else { t.c_cflag&=~PARENB; }
    t.c_iflag &= ~(INPCK|ISTRIP|IXON|IXOFF|IXANY);
    t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
    tcsetattr(fd, TCSANOW, &t);
    for (;;) {
        struct pollfd p={fd,POLLIN,0};
        if (poll(&p,1,200)>0 && (p.revents&POLLIN)) {
            uint8_t b[1024]; ssize_t r=read(fd,b,sizeof(b));
            if (r>0) { fwrite(b,1,r,stdout); fflush(stdout); }
        }
    }
}
