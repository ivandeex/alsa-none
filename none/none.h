/*-*- linux-c -*-*/

/*
 * ALSA <-> None plugins
 *
 * Copyright (c) 2006 by Pierre Ossman <ossman@cendio.se>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <alsa/asoundlib.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

typedef struct snd_none {
	int fd;
} snd_pulse_t;


