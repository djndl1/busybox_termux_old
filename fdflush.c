/*
 * Mini fdflush implementation for busybox
 *
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "internal.h"
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/fd.h>
#include <fcntl.h>


extern int fdflush_main(int argc, char **argv)
{
    int	value;
    int	fd;
    if ( **(argv+1) == '-' ) {
	usage( "fdflush device\n");
    }

    fd = open(*argv, 0);
    if ( fd < 0 ) {
	perror(*argv);
	exit(FALSE);
    }

    value = ioctl(fd, FDFLUSH, 0);
    close(fd);

    if ( value ) {
	perror(*argv);
	exit(FALSE);
    }
    exit (TRUE);
}
