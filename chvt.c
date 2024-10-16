/*
 * chvt.c - aeb - 940227 - Change virtual terminal
 *
 * busyboxed by Erik Andersen
 */
#include "internal.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

extern int getfd(void);

int
chvt_main(int argc, char** argv) 
{
    int fd, num;

    if ( ( argc != 2) || (**(argv+1) == '-' ) ) {
	usage ("chvt </dev/ttyN>\n");
    }
    fd = get_console_fd("/dev/console");
    num = atoi(argv[1]);
    if (ioctl(fd,VT_ACTIVATE,num)) {
	perror("VT_ACTIVATE");
	exit(FALSE);
    }
    if (ioctl(fd,VT_WAITACTIVE,num)) {
	perror("VT_WAITACTIVE");
	exit(FALSE);
    }
    exit( TRUE);
}

