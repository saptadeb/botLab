/** Simplified serial utilities. As opposed to my earlier
    serial class, we let you use the fd (or FILE*) directly.

    eolson@mit.edu, 2004
**/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <stdlib.h>

#ifdef __linux__
#include <linux/serial.h>
#define SUPPORT_HISPEED 1
#endif

#include <stdio.h>

#include "serial.h"


static int
serial_translate_baud (int inrate);
static int
serial_set_baud (int fd, int baud);


/** Creates a basic fd, setting baud to 9600, raw data i/o (no flow
    control, no fancy character handling. Configures it for blocking
    reads.

    Returns the fd, -1 on error
**/
int
serial_open (const char *port, int baud, int blocking)
{
    struct termios opts;

    int flags = O_RDWR | O_NOCTTY;
    if (!blocking)
        flags |= O_NONBLOCK;

    int fd = open (port, flags, 0);
    if (fd==-1)
        return -1;

    if (tcgetattr (fd, &opts)) {
        perror("tcgetattr");
        return -1;
    }

    cfsetispeed (&opts, serial_translate_baud (9600));
    cfsetospeed (&opts, serial_translate_baud (9600));

    cfmakeraw (&opts);

    // set one stop bit
    opts.c_cflag &= ~CSTOPB;

    if (tcsetattr (fd, TCSANOW, &opts)) {
        perror ("tcsetattr");
        return -1;
    }

    tcflush (fd, TCIOFLUSH);

    serial_set_baud (fd, baud);
    return fd;
}

// parity = 0: none, 1: odd, 2: even
int
serial_set_mode (int fd, int databits, int parity, int stopbits)
{
    struct termios opts;

    if (tcgetattr (fd, &opts)) {
        perror ("tcgetattr");
        return -1;
    }

    opts.c_cflag &= (~CSIZE);
    if (databits == 5)
        opts.c_cflag |= CS5;
    else if (databits == 6)
        opts.c_cflag |= CS6;
    else if (databits == 7)
        opts.c_cflag |= CS7;
    else if (databits == 8)
        opts.c_cflag |= CS8;

    opts.c_cflag &= (~PARENB);
    if (parity != 0)
        opts.c_cflag |= PARENB;

    opts.c_cflag &= (~PARODD);
    if (parity == 1)
        opts.c_cflag |= PARODD;

    opts.c_cflag &= (~CSTOPB);
    if (stopbits == 2)
        opts.c_cflag |= CSTOPB;

    if (tcsetattr(fd,TCSANOW,&opts)) {
        perror ("tcsetattr");
        return -1;
    }

    return 0;
}

int
serial_set_N82 (int fd)
{
    struct termios opts;

    if (tcgetattr (fd, &opts)) {
        perror ("tcgetattr");
        return -1;
    }

    opts.c_cflag &= ~CSIZE;
    opts.c_cflag |= CS8;
    opts.c_cflag |= CSTOPB;

    if (tcsetattr (fd, TCSANOW ,&opts)) {
        perror ("tcsetattr");
        return -1;
    }

    return 0;
}

/** Enable cts/rts flow control.
    Returns non-zero on error.
**/
int
serial_set_ctsrts (int fd, int enable)
{
    struct termios opts;

    if (tcgetattr (fd, &opts)) {
        perror ("tcgetattr");
        return -1;
    }

    if (enable)
        opts.c_cflag |= CRTSCTS;
    else
        opts.c_cflag &= (~(CRTSCTS));

    if (tcsetattr (fd, TCSANOW, &opts)) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}


/** Enable xon/xoff flow control.
    Returns non-zero on error.
**/
int
serial_set_xon (int fd, int enable)
{
    struct termios opts;

    if (tcgetattr (fd, &opts)) {
        perror ("tcgetattr");
        return -1;
    }

    if (enable)
        opts.c_iflag |= (IXON | IXOFF);
    else
        opts.c_iflag &= (~(IXON | IXOFF));

    if (tcsetattr (fd, TCSANOW, &opts)) {
        perror ("tcsetattr");
        return -1;
    }

    return 0;
}



/** Set the baud rate, where the baudrate is just the integer value
    desired.

    Returns non-zero on error.
**/
int
serial_set_baud (int fd, int baudrate)
{
    struct termios tios;
#ifdef SUPPORT_HISPEED
    struct serial_struct ser;
#endif

    int baudratecode = serial_translate_baud (baudrate);

    if (baudratecode > 0) {
        // standard baud rate.
        tcgetattr (fd, &tios);
        cfsetispeed (&tios, baudratecode);
        cfsetospeed (&tios, baudratecode);
        tcflush (fd, TCIFLUSH);
        tcsetattr (fd, TCSANOW, &tios);

#ifdef SUPPORT_HISPEED
        ioctl (fd, TIOCGSERIAL, &ser);

        ser.flags = (ser.flags&(~ASYNC_SPD_MASK));
        ser.custom_divisor = 0;

        ioctl (fd, TIOCSSERIAL, &ser);
#endif
    }
    else {
        // non-standard baud rate.
#ifdef SUPPORT_HISPEED
//        printf("Setting custom divisor\n");

        if (tcgetattr (fd, &tios))
            perror ("tcgetattr");

        cfsetispeed (&tios, B38400);
        cfsetospeed (&tios, B38400);
        tcflush (fd, TCIFLUSH);

        if (tcsetattr (fd, TCSANOW, &tios))
            perror ("tcsetattr");

        if (ioctl (fd, TIOCGSERIAL, &ser))
            perror ("ioctl TIOCGSERIAL");

        ser.flags = (ser.flags&(~ASYNC_SPD_MASK)) | ASYNC_SPD_CUST;
        ser.custom_divisor = (ser.baud_base + baudrate/2)/baudrate;
        ser.reserved_char[0] = 0; // what the hell does this do?

//        printf("baud_base %i\ndivisor %i\n", ser.baud_base,ser.custom_divisor);

        if (ioctl (fd, TIOCSSERIAL, &ser))
            perror ("ioctl TIOCSSERIAL");
#endif
    }

    tcflush (fd, TCIFLUSH);

    return 0;
}

static int
serial_translate_baud (int inrate)
{
    switch (inrate) {
        case 0:
            return B0;
        case 300:
            return B300;
        case 1200:
            return B1200;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
#ifdef SUPPORT_HISPEED
        case 460800:
            return B460800;
#endif
        default:
            return -1; // do custom divisor
    }
}

int
serial_close (int fd)
{
    return close (fd);
}


int
serial_set_dtr (int fd, int v)
{
    int status;

    if (ioctl (fd, TIOCMGET, &status)) {
        perror ("TIOCMGET");
        return -1;
    }

    if (v)
        status |= TIOCM_DTR;
    else
        status &= ~TIOCM_DTR;

    if (ioctl (fd, TIOCMSET, &status)) {
        perror ("TIOCMSET");
        return -1;
    }

    return 0;
}

int
serial_set_rts (int fd, int v)
{
    int status;

    if (ioctl (fd, TIOCMGET, &status)) {
        perror ("TIOCMGET");
        return -1;
    }

    if (v)
        status |= TIOCM_RTS;
    else
        status &= ~TIOCM_RTS;

    if (ioctl (fd, TIOCMSET, &status)) {
        perror ("TIOCMSET");
        return -1;
    }

    return 0;
}
