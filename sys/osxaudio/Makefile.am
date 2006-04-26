plugin_LTLIBRARIES = libgstosxaudio.la

libgstosxaudio_la_SOURCES = gstosxringbuffer.c		\
			    gstosxaudioelement.c	\
			    gstosxaudiosink.c

libgstosxaudio_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GSTPB_BASE_FLAGS)
libgstosxaudio_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GSTPB_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@
libgstosxaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,CoreAudio

noinst_HEADERS = gstosxaudiosink.h	\
		gstosxaudioelement.h	\
		gstosxringbuffer.h



