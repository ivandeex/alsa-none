/*-*- linux-c -*-*/

/*
 * ALSA configuration function extensions for none
 *
 * Copyright (c) 2008 by Sjoerd Simons <sjoerd@luon.net>
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 *
 */

#include <stdio.h>

#include <alsa/asoundlib.h>


/* Not actually part of the alsa api....  */
extern int
snd_config_hook_load(snd_config_t * root, snd_config_t * config,
		     snd_config_t ** dst, snd_config_t * private_data);

int
conf_none_hook_load_if_running(snd_config_t * root, snd_config_t * config,
				snd_config_t ** dst,
				snd_config_t * private_data)
{
	int ret = 0;
	*dst = NULL;
	return ret;
}

SND_DLSYM_BUILD_VERSION(conf_none_hook_load_if_running,
			SND_CONFIG_DLSYM_VERSION_HOOK);
