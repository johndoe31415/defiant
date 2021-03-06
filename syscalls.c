/**
 *	defiant - Modded Bobby Car toy for toddlers
 *	Copyright (C) 2020-2020 Johannes Bauer
 *
 *	This file is part of defiant.
 *
 *	defiant is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; this program is ONLY licensed under
 *	version 3 of the License, later versions are explicitly excluded.
 *
 *	defiant is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with defiant; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	Johannes Bauer <JohannesBauer@gmx.de>
**/

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <stm32f10x_usart.h>
#include "usart.h"
#include "syscalls.h"
#include "system.h"

extern uint8_t _ebss;
static uint8_t *current_break = &_ebss;

void *_sbrk(intptr_t increment) {
	void *retval = current_break;
	current_break += increment;
	return retval;
}

void _exit(int status) {
	while (true);
}

ssize_t _write_r(struct _reent *reent, int fd, const void *data, size_t length) {
	if ((fd == STDOUT_FILENO) || (fd == STDERR_FILENO)) {
		for (size_t i = 0; i < length; i++) {
			char c = ((char*)data)[i];
			if (c == '\n') {
				usart_transmit_char('\r');
			}
			usart_transmit_char(c);
		}
	}
	return length;
}

int _close_r(struct _reent *reent, int fd) {
	return 0;
}

int _fstat_r(struct _reent *reent, int fd, struct stat *stat) {
	return -1;
}

int _isatty_r(struct _reent *reent, int fd) {
	return 0;
}

off_t _lseek_r(struct _reent *reent, int fd, off_t offset, int whence) {
	return 0;
}

ssize_t _read_r(struct _reent *reent, int fd, const void *data, size_t length) {
	return 0;
}
