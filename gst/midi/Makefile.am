plugin_LTLIBRARIES = libgstmidi.la

libgstmidi_la_SOURCES = midi.c midiparse.c
libgstmidi_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmidi_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstmidi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmidi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = midiparse.h

if USE_ALSA
libgstmidi_la_SOURCES += alsamidisrc.c
libgstmidi_la_CFLAGS += $(ALSA_FLAGS)
libgstmidi_la_LIBADD += $(ALSA_LIBS)
noinst_HEADERS += alsamidisrc.h
endif
