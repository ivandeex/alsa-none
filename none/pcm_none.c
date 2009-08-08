/*-*- linux-c -*-*/

#include <stdio.h>
#include <math.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

#define NONE_CHANNELS_MAX	1
#define NONE_RATE_MIN		4000
#define NONE_RATE_MAX		48000
#define NONE_MAX_BUFFER_BYTES	(256 * 1024)
#define NONE_MAX_PERIOD_BYTES	(32 * 1024)
#define NONE_MAX_PERIODS	1024

#define NONE_RATE_MULTIPLIER	1.

typedef struct pcm_none {
	snd_pcm_ioplug_t io;
	struct pcm_none *next;
	int id;
	int playback;
	int running;
	int poll_fd;
	int other_fd;
	int trig_num;
	int auto_advance;
	size_t usr_ptr;
	size_t adv_ptr;
	size_t frame_size;
	size_t rate;
	size_t fire_threshold;
	struct timespec adv_time_0;
	size_t adv_ptr_0;
	int reset_stream;
} none_t;

static none_t *none_chain;

static int enable_debug = 0;

static int debug (const none_t *pcm, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	struct timeval tv;
	if (!enable_debug)
		return 0;
	gettimeofday(&tv, NULL);
	sprintf(buf, "%02d.%03d.%03d %d%c ",
		(int)(tv.tv_sec % 60), (int)(tv.tv_usec / 1000), (int)(tv.tv_usec % 1000),
		pcm ? pcm->id : 0, pcm ? (pcm->playback ? 'P' : 'C') : 'X');
	va_start(ap, fmt);
	vsprintf(buf + strlen(buf), fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	write(2, buf, strlen(buf));
	return 0;
}

static int none_fire_pipe(none_t *pcm, int size)
{
	char ch = 0;
	if (pcm->playback)
		return -1;
	if (pcm->trig_num > 0)
		return -2;
	if (size < pcm->fire_threshold)
		return -3;
	write(pcm->other_fd, &ch, 1);
	pcm->trig_num ++;
	debug(pcm, "pipe fired");
	return 0;
}

static int none_flush_pipe(none_t *pcm)
{
	char ch;
	if (pcm->playback)
		return -1;
	if (pcm->trig_num)
		debug(pcm, "pipe flushed");
	while (pcm->trig_num > 0) {
		read(pcm->poll_fd, &ch, 1);
		pcm->trig_num --;
	}
	return 0;
}

static int none_advance(none_t *pcm)
{
	struct timespec now;
	double sec;
	size_t adv_ptr;
	if (!pcm->running)
		return -1;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (pcm->reset_stream) {
		pcm->adv_time_0 = now;
		pcm->adv_ptr_0 = pcm->adv_ptr;
		pcm->reset_stream = 0;
	}
	sec = (now.tv_sec - pcm->adv_time_0.tv_sec) * 1.
		+ (now.tv_nsec - pcm->adv_time_0.tv_nsec) * 1e-9;
	adv_ptr = (size_t)(pcm->adv_ptr_0 + sec * pcm->rate * NONE_RATE_MULTIPLIER);
	debug(pcm, "advance: %u to %u   (hw:%u ap:%u)",
		pcm->adv_ptr, adv_ptr, pcm->io.hw_ptr, pcm->io.appl_ptr);
	if (adv_ptr > pcm->adv_ptr) {
		none_fire_pipe(pcm, adv_ptr - pcm->adv_ptr);
		pcm->adv_ptr = adv_ptr;
	}
	return 0;
}

static snd_pcm_sframes_t none_pointer(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	if (!pcm->running) {
		debug(pcm, "pointer: not running");
		return 0;
	}
	debug(pcm, "pointer: %u", pcm->adv_ptr);
	if (pcm->auto_advance)
		none_advance(pcm);
	return pcm->adv_ptr;
}

static snd_pcm_sframes_t none_transfer(snd_pcm_ioplug_t * io,
				     const snd_pcm_channel_area_t * areas,
				     snd_pcm_uframes_t offset,
				     snd_pcm_uframes_t size)
{
	struct timeval req;
	double sec;
	long delta;
	none_t *pcm = io->private_data;
	assert(pcm);
	if (!pcm->running) {
		debug(pcm, "transfer: not running");
		return 0;
	}

	debug(pcm, "transfer: offset:%u size: %u", offset, size);
	for (;;) {
		none_advance(pcm);
		delta = pcm->usr_ptr + size - pcm->adv_ptr;
		debug(pcm, "transfer: remaining:%ld", -delta);
		if (delta <= 0)
			break;
		sec = (double)delta / (pcm->rate * NONE_RATE_MULTIPLIER);
		if (sec < 1e-6)
			break;
		debug(pcm, "transfer: sleep %d ms", (int)(sec * 1e3));

		// sleep
		req.tv_sec = (time_t) sec;
		req.tv_usec = (long)((sec - floor(sec)) * 1e6);
		if (req.tv_usec > 1000000L) {
			req.tv_sec += 1;
			req.tv_usec -= 1000000L;
		}
		select(0, NULL, NULL, NULL, &req);
	}
	debug(pcm, "transfer: return:%ld", (long)size);
	pcm->usr_ptr += size;
	if (!pcm->playback) {
		char *buf = (char *) areas->addr + (areas->first + areas->step * offset) / 8;
		memset(buf, 0, size * pcm->frame_size);
		none_flush_pipe(pcm);
		none_advance(pcm);
	}
	if (pcm->playback) {
		// playback device will fire capture devices, if any
		none_t *n;
		for (n = none_chain; n; n = n->next) {
			if (n->running && !n->playback) {
				debug(pcm, "trigger device %d", n->id);
				none_advance(n);
			}
		}
	}
	return size;
}

static int none_pcm_poll_revents(snd_pcm_ioplug_t * io,
				  struct pollfd *pfd, unsigned int nfds,
				  unsigned short *revents)
{
	none_t *pcm = io->private_data;
	long size;
	assert(pcm);
	if (!pcm->running) {
		debug(pcm, "poll: not running");
		return 0;
	}
	none_advance(pcm);
	size = pcm->adv_ptr - pcm->usr_ptr;
	if (size < pcm->fire_threshold)
		*revents = 0;
	else
		*revents = pcm->playback ? POLLOUT : POLLIN;
	debug(pcm, "poll: events:%d size:%ld", (unsigned) *revents, size);
	return 0;
}

static int none_start(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	if (pcm->running) {
		debug(pcm, "start: already started");
		return 0;
	}
	debug(pcm, "start");
	pcm->running = 1;
	pcm->reset_stream = 1;
	none_advance(pcm);
	return 0;
}

static int none_stop(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	if (!pcm->running) {
		debug(pcm, "stop: already stopped");
		return 0;
	}
	debug(pcm, "stop");
	pcm->running = 0;
	none_flush_pipe(pcm);
	return 0;
}

static int none_drain(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	debug(pcm, "drain");
	if (pcm->running)
		none_stop(io);
	return 0;
}

static int none_prepare(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	debug(pcm, "prepare");
	pcm->adv_ptr = pcm->usr_ptr = 0;
	if (pcm->playback)
		none_start(io);
	return 0;
}

static int none_delay(snd_pcm_ioplug_t * io, snd_pcm_sframes_t * delayp)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	debug(pcm, "delay");
	*delayp = 1;
	return 0;
}

static int none_hw_params(snd_pcm_ioplug_t *io, snd_pcm_hw_params_t * params)
{
	none_t *pcm = io->private_data;
	assert(pcm);
	pcm->frame_size = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;
	pcm->rate = io->rate;
	pcm->fire_threshold = io->period_size;
	if ((int)pcm->frame_size < 1) {
		debug(pcm, "params: invalid frame size %d", (int)pcm->frame_size);
		pcm->frame_size = 1;
		return -EINVAL;
	}
	switch (io->format) {
	case SND_PCM_FORMAT_U8:
	case SND_PCM_FORMAT_A_LAW:
	case SND_PCM_FORMAT_MU_LAW:
	case SND_PCM_FORMAT_S16_LE:
	case SND_PCM_FORMAT_S16_BE:
		break;
	default:
		debug(pcm, "params: invalid format %d", io->format);
		return -EINVAL;
	}
	debug(pcm, "params: frame_sz:%d rate:%u format:%d channels:%u period_sz:%u bufsize:%u",
		pcm->frame_size, pcm->rate, io->format, io->channels,
		io->period_size, io->buffer_size);

	return 0;
}

static int none_close(snd_pcm_ioplug_t * io)
{
	none_t *pcm = io->private_data;
	none_t *n;
	assert(pcm);
	debug(pcm, "close");
	if (none_chain == pcm) {
		none_chain = pcm->next;
	} else {
		for (n = none_chain; n; n = n->next) {
			if (n->next == pcm) {
				n->next = pcm->next;
				break;
			}
		}
	}
	if (pcm->running)
		none_stop(io);
	close(pcm->poll_fd);
	close(pcm->other_fd);
	free(pcm);
	return 0;
}

static const snd_pcm_ioplug_callback_t none_callbacks = {
	.start = none_start,
	.stop = none_stop,
	.drain = none_drain,
	.pointer = none_pointer,
	.transfer = none_transfer,
	.delay = none_delay,
	.poll_revents = none_pcm_poll_revents,
	.prepare = none_prepare,
	.hw_params = none_hw_params,
	.close = none_close,
};

static int none_hw_constraint(none_t * pcm)
{
	snd_pcm_ioplug_t *io = &pcm->io;
	int err;

	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED
	};

	static const unsigned int formats[] = {
		SND_PCM_FORMAT_U8,
		SND_PCM_FORMAT_S16_LE,
		SND_PCM_FORMAT_S16_BE,
	};

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
					    ARRAY_SIZE(access_list),
					    access_list);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
						ARRAY_SIZE(formats), formats);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
						1, NONE_CHANNELS_MAX);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
						NONE_RATE_MIN, NONE_RATE_MAX);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
						1, NONE_MAX_BUFFER_BYTES);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
						128, NONE_MAX_PERIOD_BYTES);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 3, NONE_MAX_PERIODS);
	if (err < 0)
		return err;

	debug(pcm, "constraint");
	return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(none)
{
	static int none_id;
	snd_config_iterator_t i, next;
	int err, fds[2];
	int auto_advance = 0;
	long value;
	none_t *pcm;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "debug") == 0) {
			if ((err = snd_config_get_integer(n, &value)) >= 0) {
				enable_debug = (int) value;
				continue;
			}
		} else if (strcmp(id, "auto_advance") == 0) {
			if ((err = snd_config_get_integer(n, &value)) >= 0) {
				auto_advance = (int) value;
				continue;
			}
		} else if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint")) {
			continue;
		}
		fprintf(stderr, "open: invalid id \"%s\"", id);
		return -EINVAL;
	}

	if (NULL == (pcm = calloc(1, sizeof(none_t))))
		return -ENOMEM;

	pcm->id = ++ none_id;
	pcm->running = 0;
	pcm->playback = stream == SND_PCM_STREAM_PLAYBACK;

	if (pipe(fds) < 0) {
		free(pcm);
		return -EIO;
	}
	pcm->poll_fd = fds[pcm->playback];
	pcm->other_fd = fds[1 - pcm->playback];
	pcm->trig_num = 0;

	pcm->reset_stream = 1;
	pcm->frame_size = 2;
	pcm->rate = 48000;

	pcm->fire_threshold = 1;
	pcm->adv_ptr = pcm->usr_ptr = 0;
	pcm->auto_advance = auto_advance;

	pcm->io.version = SND_PCM_IOPLUG_VERSION;
	pcm->io.name = "ALSA <-> NoNe PCM I/O Plugin";
	pcm->io.poll_fd = pcm->poll_fd;
	pcm->io.poll_events = pcm->playback ? POLLOUT : POLLIN;
	pcm->io.mmap_rw = 0;
	pcm->io.private_data = pcm;
	pcm->io.callback = &none_callbacks;

	err = snd_pcm_ioplug_create(&pcm->io, name, stream, mode);
	if (err < 0) {
		debug(pcm, "ioplug_create failed");
		goto error;
	}

	err = none_hw_constraint(pcm);
	if (err < 0) {
		snd_pcm_ioplug_delete(&pcm->io);
		debug(pcm, "hw_contraint failed error:%d", err);
		goto error;
	}

	*pcmp = pcm->io.pcm;

	pcm->next = none_chain;
	none_chain = pcm;

	debug(pcm, "open");
	return 0;

error:
	close(fds[0]);
	close(fds[1]);
	free(pcm);
	debug(0, "open failed error:%d", err);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(none);

