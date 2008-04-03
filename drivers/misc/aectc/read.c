/*
 * read.c -- simple application for reading registers from a Adrienne
 * Electronics Corp time code device under Linux
 *
 * Copyright (C) 2008 Brandon Philips <brandon@ifup.org>
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc., 59
 *   Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "aectc.h"

int main(int argc, char **argv)
{
	struct aectc_reg reg;
	int fd;

	if (argc == 4) {
		sscanf(argv[1], "%lx", &reg.reg);
		reg.length = atoi(argv[2]);
	} else {
		fprintf(stderr, "Usage: %s reg length file\n", argv[0]);
		exit(1);
	}

	fd = open(argv[3], O_RDWR);

	if (fd == -1) {
		fprintf(stderr, "Couldn't open %s\n", argv[3]);
		exit(1);
	}

	if (ioctl(fd, AEC_IOC_READREG, &reg) < 0) {
		fprintf(stderr,"%s: ioctl(stdin, AEC_IOC_READREG): %s\n",
			argv[0], strerror(errno));
		exit(1);
	}

	printf("%x\n", reg.data);
	exit(0);
}

