/*-*- linux-c -*-*/

#include <byteswap.h>
#include <limits.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#define DEBUG 0

#if DEBUG

#define prn(x...) _prn(x)

static int debug = 1;

static int _prn (const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	struct timeval tv;
	if (!debug)
		return 0;
	gettimeofday(&tv, NULL);
	sprintf(buf, "%02d.%03d.%03d ",
		(int)(tv.tv_sec % 60), (int)(tv.tv_usec / 1000), (int)(tv.tv_usec % 1000));
	va_start(ap, fmt);
	vsprintf(buf + strlen(buf), fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	write(2, buf, strlen(buf));
	return 0;
}

#else

#define prn(x...)

#endif /* DEBUG */

#define ppp prn("<<<%s/%d>>>", __FUNCTION__, __LINE__);

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

#define NONE_CHANNELS_MAX	32U
#define NONE_RATE_MAX		(48000U*4U)

typedef struct {
	snd_pcm_ioplug_t io;
	int state;
	int poll_fd, trig_fd;
	size_t last_size;
	size_t ptr;
	size_t ptr_add;
	size_t offset;
	size_t frame_size;
	size_t buffer_attr_tlength;
} snd_pcm_none_t;

#define LATENCY_BYTES 512
#define MAX_ADD 1024

static snd_pcm_sframes_t none_pointer(snd_pcm_ioplug_t * io)
{
	snd_pcm_none_t *pcm = io->private_data;
	snd_pcm_sframes_t ret = 0;
	assert(pcm);
#if 0
	if (io->state != SND_PCM_STATE_RUNNING) {
		prn("pointer: not running");
		return 0;
	}
#endif
	int add = pcm->ptr_add;
	if (add > MAX_ADD) {
		add = MAX_ADD;
	} else {
		add = pcm->ptr_add;
	}
	pcm->ptr += add;
	pcm->ptr_add -= add;
	prn("pointer: %s ptr=%d add=%d ret=%d", io->stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture", pcm->ptr, pcm->ptr_add, (int)ret);
	ret = snd_pcm_bytes_to_frames(io->pcm, pcm->ptr);
	return ret;
}

static snd_pcm_sframes_t none_write(snd_pcm_ioplug_t * io,
				     const snd_pcm_channel_area_t * areas,
				     snd_pcm_uframes_t offset,
				     snd_pcm_uframes_t size)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
#if 0
	if (io->state != SND_PCM_STATE_RUNNING) {
		prn("write: not running");
		return 0;
	}
#endif
	size_t frame_size = pcm->frame_size;
	pcm->ptr_add += size * frame_size;
	prn("write: %s size=%d ptr=%d framesize=%d", io->stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture", (int)size, pcm->ptr, frame_size);
	return size;
}

static snd_pcm_sframes_t none_read(snd_pcm_ioplug_t * io,
				    const snd_pcm_channel_area_t * areas,
				    snd_pcm_uframes_t offset,
				    snd_pcm_uframes_t size)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
#if 0
	if (io->state != SND_PCM_STATE_RUNNING) {
		prn("read: not running");
		return 0;
	}
#endif
	snd_pcm_sframes_t ret;
	size_t remain_size, frag_length;
	size_t frame_size = pcm->frame_size;
	remain_size = size * frame_size;
	frag_length = 0;
	if (frag_length > remain_size) {
		pcm->offset += remain_size;
		frag_length = remain_size;
	} else {
		pcm->offset = 0;
	}
	remain_size -= frag_length;
	ret = size - remain_size / frame_size;
	prn("read: %s size=%d ret=%d", io->stream == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture", (int)size, (int)ret);
	return ret;
}

static int none_pcm_poll_revents(snd_pcm_ioplug_t * io,
				  struct pollfd *pfd, unsigned int nfds,
				  unsigned short *revents)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	if (io->stream == SND_PCM_STREAM_PLAYBACK) {
		*revents = pcm->ptr_add ? 0 : POLLOUT;
		prn("pollout: %x", (int)*revents);
	} else {
		*revents = 0; //POLLIN;
		prn("pollin");
	}
	return 0;
}

static int none_start(snd_pcm_ioplug_t * io)
{
	return 0;
}

static int none_stop(snd_pcm_ioplug_t * io)
{
	return 0;
}

static int none_drain(snd_pcm_ioplug_t * io)
{
	return 0;
}

static int none_prepare(snd_pcm_ioplug_t * io)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	pcm->offset = 0;
	return 0;
}

static int none_delay(snd_pcm_ioplug_t * io, snd_pcm_sframes_t * delayp)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	*delayp = snd_pcm_bytes_to_frames(io->pcm, LATENCY_BYTES);
	return 0;
}

static int none_hw_params(snd_pcm_ioplug_t *io, snd_pcm_hw_params_t * params)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	pcm->frame_size = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;
	switch (io->format) {
	case SND_PCM_FORMAT_U8:
	case SND_PCM_FORMAT_A_LAW:
	case SND_PCM_FORMAT_MU_LAW:
	case SND_PCM_FORMAT_S16_LE:
	case SND_PCM_FORMAT_S16_BE:
		break;
	default:
		SNDERR("None: Unsupported format %s\n", snd_pcm_format_name(io->format));
		return -EINVAL;
	}
	return 0;
}

static int none_close(snd_pcm_ioplug_t * io)
{
	snd_pcm_none_t *pcm = io->private_data;
	assert(pcm);
	close(pcm->poll_fd);
	close(pcm->trig_fd);
	free(pcm);
	return 0;
}

static const snd_pcm_ioplug_callback_t none_playback_callback = {
	.start = none_start,
	.stop = none_stop,
	.drain = none_drain,
	.pointer = none_pointer,
	.transfer = none_write,
	.delay = none_delay,
	.poll_revents = none_pcm_poll_revents,
	.prepare = none_prepare,
	.hw_params = none_hw_params,
	.close = none_close,
};

static const snd_pcm_ioplug_callback_t none_capture_callback = {
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

static int none_hw_constraint(snd_pcm_none_t * pcm)
{
	snd_pcm_ioplug_t *io = &pcm->io;

	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED
	};
	static const unsigned int formats[] = {
		SND_PCM_FORMAT_U8,
		SND_PCM_FORMAT_A_LAW,
		SND_PCM_FORMAT_MU_LAW,
		SND_PCM_FORMAT_S16_LE,
		SND_PCM_FORMAT_S16_BE,
		SND_PCM_FORMAT_FLOAT_LE,
		SND_PCM_FORMAT_FLOAT_BE,
		SND_PCM_FORMAT_S32_LE,
		SND_PCM_FORMAT_S32_BE
	};

	int err;

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
					    ARRAY_SIZE(access_list),
					    access_list);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
					    ARRAY_SIZE(formats), formats);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
					    1, NONE_CHANNELS_MAX);
	if (err < 0)
		return err;

	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
					      1, NONE_RATE_MAX);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io,
					    SND_PCM_IOPLUG_HW_BUFFER_BYTES,
					    1, 4 * 1024 * 1024);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io,
					    SND_PCM_IOPLUG_HW_PERIOD_BYTES,
					    128, 2 * 1024 * 1024);
	if (err < 0)
		return err;

	err =
	    snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS,
					    3, 1024);
	if (err < 0)
		return err;

	return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(none)
{
	snd_config_iterator_t i, next;
	int err, fds[2];
	snd_pcm_none_t *pcm;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if (pipe(fds) < 0) {
		SYSERR("Cannot create pipe");
		return -errno;
	}

	pcm = calloc(1, sizeof(snd_pcm_none_t));
	if (!pcm) {
		close(fds[0]);
		close(fds[1]);
		return -ENOMEM;
	}

	pcm->poll_fd = fds[0];
	pcm->trig_fd = fds[1];
	pcm->state = SND_PCM_STATE_OPEN;

	pcm->io.version = SND_PCM_IOPLUG_VERSION;
	pcm->io.name = "ALSA <-> NONE PCM I/O Plugin";
	pcm->io.poll_fd = pcm->poll_fd;
	pcm->io.poll_events = stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
	pcm->io.mmap_rw = 0;
	pcm->io.private_data = pcm;
	pcm->io.callback = stream == SND_PCM_STREAM_PLAYBACK ?
			&none_playback_callback : &none_capture_callback;

	err = snd_pcm_ioplug_create(&pcm->io, name, stream, mode);
	if (err < 0)
		goto error;

	err = none_hw_constraint(pcm);
	if (err < 0) {
		snd_pcm_ioplug_delete(&pcm->io);
		goto error;
	}

	*pcmp = pcm->io.pcm;
	return 0;

error:
	free(pcm);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(none);

