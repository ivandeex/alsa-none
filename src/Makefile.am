asound_module_pcm_LTLIBRARIES = libasound_module_pcm_none.la
asound_module_ctl_LTLIBRARIES = libasound_module_ctl_none.la

asound_module_pcmdir = @ALSA_PLUGIN_DIR@
asound_module_ctldir = @ALSA_PLUGIN_DIR@

AM_CFLAGS = -Wall -g @ALSA_CFLAGS@ $(PTHREAD_CFLAGS) $(none_CFLAGS) -D_GNU_SOURCE
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined $(LDFLAGS_NOUNDEFINED)

libasound_module_ctl_none_la_SOURCES = ctl_none.c
libasound_module_ctl_none_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(none_LIBS)

libasound_module_pcm_none_la_SOURCES = pcm_none.c
libasound_module_pcm_none_la_LIBADD = @ALSA_LIBS@ $(PTHREAD_LIBS) $(none_LIBS) -lrt -lm

