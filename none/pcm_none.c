/*-*- linux-c -*-*/

#include <byteswap.h>
#include <limits.h>
#include <sys/shm.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

typedef struct {
	snd_pcm_ioplug_t io;
	int state;
	int poll_fd;
} snd_pcm_none_t;

static int none_close(snd_pcm_ioplug_t *pcm)
{
	snd_pcm_none_t *none = pcm->private_data;
	close(none->poll_fd);
	free(none);
	return 0;
}

static int none_delay(snd_pcm_ioplug_t *pcm ATTRIBUTE_UNUSED, snd_pcm_sframes_t *delayp)
{
	*delayp = 0;
	return 0;
}

static int none_prepare(snd_pcm_ioplug_t *pcm)
{
	snd_pcm_none_t *none = pcm->private_data;
	none->state = SND_PCM_STATE_PREPARED;
	return 0;
}

static int none_start(snd_pcm_ioplug_t *pcm)
{
	snd_pcm_none_t *none = pcm->private_data;
	none->state = SND_PCM_STATE_RUNNING;
	return 0;
}

static int none_hw_params(snd_pcm_ioplug_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t * params ATTRIBUTE_UNUSED)
{
	return 0;
}

static snd_pcm_sframes_t none_pointer(snd_pcm_ioplug_t * io)
{
	return 0;
}

static int none_stop(snd_pcm_ioplug_t * io)
{
	return 0;
}

static int none_pcm_poll_revents(snd_pcm_ioplug_t * io,
				  struct pollfd *pfd, unsigned int nfds,
				  unsigned short *revents)
{
	int err = 0;
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	if (err)
		*revents = io->stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	else
		*revents = 0;
	return err;
}

static snd_pcm_sframes_t none_read(snd_pcm_ioplug_t * io,
				    const snd_pcm_channel_area_t * areas,
				    snd_pcm_uframes_t offset,
				    snd_pcm_uframes_t size)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	void *dst_buf = (char *) areas->addr + (areas->first + areas->step * offset) / 8;
	memset(dst_buf, 0, size);
	return size;
}


static const snd_pcm_ioplug_callback_t none_callback = {
	.start = none_start,
	.stop = none_stop,
	.pointer = none_pointer,
	.transfer = none_read,
	.delay = none_delay,
	.poll_revents = none_pcm_poll_revents,
	.prepare = none_prepare,
	.hw_params = none_hw_params,
	.close = none_close,
};

/*
None plugin discards contents of a PCM stream or creates a stream with zero
samples.

Note: This implementation uses device /dev/null (playback and capture)

pcm.name {
        type none               # Null PCM
}
*/

SND_PCM_PLUGIN_DEFINE_FUNC(none)
{
	snd_config_iterator_t i, next;
	int err, poll_fd;
	snd_pcm_none_t *pcm;

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

	poll_fd = open("/dev/null", O_WRONLY);
	if (poll_fd < 0) {
		SYSERR("Cannot open /dev/null");
		return -errno;
	}

	pcm = calloc(1, sizeof(snd_pcm_none_t));
	if (!pcm) {
		close(poll_fd);
		return -ENOMEM;
	}
	pcm->poll_fd = poll_fd;
	pcm->state = SND_PCM_STATE_OPEN;

	pcm->io.version = SND_PCM_IOPLUG_VERSION;
	pcm->io.name = "ALSA <-> NONE PCM I/O Plugin";
	pcm->io.poll_fd = poll_fd;
	pcm->io.poll_events = stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	pcm->io.mmap_rw = 0;
	pcm->io.private_data = pcm;

	err = snd_pcm_ioplug_create(&pcm->io, name, stream, mode);
	if (err < 0)
		goto error;

	*pcmp = pcm->io.pcm;
	return 0;

error:
	free(pcm);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(pulse);

