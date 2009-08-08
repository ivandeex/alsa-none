/*-*- linux-c -*-*/

/*
 * ALSA <-> None mixer control plugin
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
#include <alsa/control_external.h>


typedef struct snd_ctl_none {
	snd_ctl_ext_t ext;
	int sink_volume;
	int source_volume;
	int sink_muted;
	int source_muted;
} snd_ctl_none_t;

#define SOURCE_VOL_NAME "Capture Volume"
#define SOURCE_MUTE_NAME "Capture Switch"
#define SINK_VOL_NAME "Playback Volume"
#define SINK_MUTE_NAME "Playback Switch"

#define UPDATE_SINK_VOL     0x01
#define UPDATE_SINK_MUTE    0x02
#define UPDATE_SOURCE_VOL   0x04
#define UPDATE_SOURCE_MUTE  0x08

static int none_elem_count(snd_ctl_ext_t * ext)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	return 4;
}

static int none_elem_list(snd_ctl_ext_t * ext, unsigned int offset,
			   snd_ctl_elem_id_t * id)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);

	switch (offset) {
	case 0:	snd_ctl_elem_id_set_name(id, SOURCE_VOL_NAME); break;
	case 1:	snd_ctl_elem_id_set_name(id, SOURCE_MUTE_NAME); break;
	case 2: snd_ctl_elem_id_set_name(id, SINK_VOL_NAME); break;
	case 3:	snd_ctl_elem_id_set_name(id, SINK_MUTE_NAME); break;
	}

	return 0;
}

static snd_ctl_ext_key_t none_find_elem(snd_ctl_ext_t * ext,
					 const snd_ctl_elem_id_t * id)
{
	const char *name;
	unsigned int numid;

	numid = snd_ctl_elem_id_get_numid(id);
	if (numid > 0 && numid <= 4)
		return numid - 1;

	name = snd_ctl_elem_id_get_name(id);

	if (strcmp(name, SOURCE_VOL_NAME) == 0)
		return 0;
	if (strcmp(name, SOURCE_MUTE_NAME) == 0)
		return 1;
	if (strcmp(name, SINK_VOL_NAME) == 0)
		return 2;
	if (strcmp(name, SINK_MUTE_NAME) == 0)
		return 3;

	return SND_CTL_EXT_KEY_NOT_FOUND;
}

static int none_get_attribute(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
			       int *type, unsigned int *acc,
			       unsigned int *count)
{
	snd_ctl_none_t *ctl = ext->private_data;
	if (key < 0 || key > 3)
		return -EINVAL;
	assert(ctl);
	*type = key & 1 ? SND_CTL_ELEM_TYPE_BOOLEAN : SND_CTL_ELEM_TYPE_INTEGER;
	*acc = SND_CTL_EXT_ACCESS_READWRITE;
	*count = 1;
	return 0;
}

static int none_get_integer_info(snd_ctl_ext_t * ext,
				  snd_ctl_ext_key_t key, long *imin,
				  long *imax, long *istep)
{
	*istep = 1;
	*imin = 0;
	*imax = 127;
	return 0;
}

static int none_read_integer(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
			      long *value)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	switch (key) {
	case 0:
		*value = ctl->source_volume;
		break;
	case 1:
		*value = !ctl->source_muted;
		break;
	case 2:
		*value = ctl->sink_volume;
		break;
	case 3:
		*value = !ctl->sink_muted;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int none_write_integer(snd_ctl_ext_t * ext, snd_ctl_ext_key_t key,
			       long *value)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	switch (key) {
	case 0:
		ctl->source_volume = *value;
		break;
	case 1:
		ctl->source_muted = !*value;
		break;
	case 2:
		ctl->sink_volume = *value;
		break;
	case 3:
		ctl->sink_muted = !*value;
		break;
	default:
		return -EINVAL;
	}
	return 1;
}

static void none_close(snd_ctl_ext_t * ext)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	free(ctl);
}

static void none_subscribe_events(snd_ctl_ext_t * ext, int subscribe)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
}

static int none_read_event(snd_ctl_ext_t * ext, snd_ctl_elem_id_t * id,
			    unsigned int *event_mask)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	return -EAGAIN;
}

static int none_ctl_poll_revents(snd_ctl_ext_t * ext, struct pollfd *pfd,
				  unsigned int nfds,
				  unsigned short *revents)
{
	snd_ctl_none_t *ctl = ext->private_data;
	assert(ctl);
	*revents = 0;
	return 0;
}

static const snd_ctl_ext_callback_t none_ext_callback = {
	.elem_count = none_elem_count,
	.elem_list = none_elem_list,
	.find_elem = none_find_elem,
	.get_attribute = none_get_attribute,
	.get_integer_info = none_get_integer_info,
	.read_integer = none_read_integer,
	.write_integer = none_write_integer,
	.subscribe_events = none_subscribe_events,
	.read_event = none_read_event,
	.poll_revents = none_ctl_poll_revents,
	.close = none_close,
};

SND_CTL_PLUGIN_DEFINE_FUNC(none)
{
	snd_config_iterator_t i, next;
	int err;
	snd_ctl_none_t *ctl;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0
		    || strcmp(id, "hint") == 0)
			continue;
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	ctl = calloc(1, sizeof(*ctl));
	if (!ctl)
		return -ENOMEM;

	ctl->ext.version = SND_CTL_EXT_VERSION;
	ctl->ext.card_idx = 0;
	strncpy(ctl->ext.id, "none", sizeof(ctl->ext.id) - 1);
	strncpy(ctl->ext.driver, "None plugin", sizeof(ctl->ext.driver) - 1);
	strncpy(ctl->ext.name, "None", sizeof(ctl->ext.name) - 1);
	strncpy(ctl->ext.longname, "None", sizeof(ctl->ext.longname) - 1);
	strncpy(ctl->ext.mixername, "None", sizeof(ctl->ext.mixername) - 1);
	ctl->ext.poll_fd = 0;

	ctl->ext.callback = &none_ext_callback;
	ctl->ext.private_data = ctl;

	err = snd_ctl_ext_create(&ctl->ext, name, mode);
	if (err < 0)
		goto error;

	*handlep = ctl->ext.handle;

	return 0;

error:
	free(ctl);
	return err;
}

SND_CTL_PLUGIN_SYMBOL(none);

