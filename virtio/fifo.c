/**
 * Copyright (c) 2010 Himanshu Chauhan.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file fifo.c
 * @author Himanshu Chauhan (hschauhan@nulltrace.org)
 * @author Anup Patel (anup@brainfault.org)
 * @brief source file for generic first-in-first-out queue.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "virtio.h"
#include "utils.h"
#include "fifo.h"

struct fifo *fifo_alloc(struct virtio_device *dev, uint32_t element_size,
			uint32_t element_count)
{
	struct fifo *f;

	if (!element_size || !element_count) {
		return NULL;
	}

	f = (struct fifo *)my_zalloc(dev, sizeof(struct fifo));
	if (!f) {
		return NULL;
	}

	f->elements = (void *)my_zalloc(dev, element_size * element_count);
	if (!f->elements) {
		my_free(dev, (uint64_t)f, sizeof(struct fifo));
		return NULL;
	}
	f->element_size = element_size;
	f->element_count = element_count;

	f->read_pos = 0;
	f->write_pos = 0;
	f->avail_count = 0;

	return f;
}

int fifo_free(struct virtio_device *dev, struct fifo *f)
{
	my_free(dev, (uint64_t)f->elements, f->element_size);
	my_free(dev, (uint64_t)f, sizeof(struct fifo));

	return 0;
}

#define __fifo_isempty(f)	(((f)->avail_count) ? false : true)

bool fifo_isempty(struct fifo *f)
{
	bool ret;

	if (!f) {
		return true;
	}

	ret = __fifo_isempty(f);

	return ret;
}

#define __fifo_isfull(f)	(((f)->avail_count >= (f)->element_count) ? \
							true : false)

bool fifo_isfull(struct fifo *f)
{
	bool ret;

	if (!f) {
		return false;
	}

	ret = __fifo_isfull(f);

	return ret;
}

bool fifo_enqueue(struct fifo *f, void *src, bool overwrite)
{
	bool ret = false;

	if (!f || !src) {
		return false;
	}


	if (overwrite && __fifo_isfull(f)) {
		f->read_pos++;
		if (f->element_count <= f->read_pos) {
			f->read_pos = 0;
		}
		f->avail_count--;
	}

	if (!__fifo_isfull(f)) {
		switch(f->element_size) {
		case 1:
			*((uint8_t *)(f->elements + f->write_pos))
			= *((uint8_t *)src);
			break;
		case 2:
			*((uint16_t *)(f->elements + (f->write_pos * 2)))
			= *((uint16_t *)src);
			break;
		case 4:
			*((uint32_t *)(f->elements + (f->write_pos * 4)))
			= *((uint32_t *)src);
			break;
		case 8:
			*((uint64_t *)(f->elements + (f->write_pos * 8)))
			= *((uint64_t *)src);
			break;
		default:
			memcpy(f->elements + (f->write_pos * f->element_size),
				src, f->element_size);
			break;
		};
		f->write_pos++;
		if (f->element_count <= f->write_pos) {
			f->write_pos = 0;
		}
		f->avail_count++;
		ret = true;
	}


	return ret;
}

bool fifo_dequeue(struct fifo *f, void *dst)
{
	bool ret = false;

	if (!f || !dst) {
		return false;
	}


	if (!__fifo_isempty(f)) {
		switch (f->element_size) {
		case 1:
			*((uint8_t *)dst) = *((uint8_t *)(f->elements + f->read_pos));
			break;
		case 2:
			*((uint16_t *)dst) =
				*((uint16_t *)(f->elements + (f->read_pos * 2)));
			break;
		case 4:
			*((uint32_t *)dst) =
				*((uint32_t *)(f->elements + (f->read_pos * 4)));
			break;
		case 8:
			*((uint64_t *)dst) =
				*((uint64_t *)(f->elements + (f->read_pos * 8)));
			break;
		default:
			memcpy(dst,
				f->elements + (f->read_pos * f->element_size),
				f->element_size);
			break;
		};
		f->read_pos++;
		if (f->element_count <= f->read_pos) {
			f->read_pos = 0;
		}
		f->avail_count--;
		ret = true;
	}

	return ret;
}

bool fifo_clear(struct fifo *f)
{

	if (!f) {
		return false;
	}


	f->read_pos = 0;
	f->write_pos = 0;
	f->avail_count = 0;

	return true;
}

bool fifo_getelement(struct fifo *f, uint32_t index, void *dst)
{
	if (!f || !dst) {
		return false;
	}

	if (f->element_count <= index) {
		return false;
	}

	index = (f->read_pos + index);
	if (f->element_count <= index) {
		index -= f->element_count;
	}

	switch(f->element_size) {
	case 1:
		*((uint8_t *)dst) = *((uint8_t *)(f->elements + index));
		break;
	case 2:
		*((uint16_t *)dst) = *((uint16_t *)(f->elements + (index * 2)));
		break;
	case 4:
		*((uint32_t *)dst) = *((uint32_t *)(f->elements + (index * 4)));
		break;
	case 8:
		*((uint64_t *)dst) = *((uint64_t *)(f->elements + (index * 8)));
		break;
	default:
		memcpy(dst,
			f->elements + (index * f->element_size),
			f->element_size);
		break;
	};

	return true;
}

uint32_t fifo_avail(struct fifo *f)
{
	uint32_t ret;

	if (!f) {
		return 0;
	}

	ret = f->avail_count;

	return ret;
}

