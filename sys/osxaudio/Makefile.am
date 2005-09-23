plugin_LTLIBRARIES = libgstosxaudio.la

libgstosxaudio_la_SOURCES = gstosxaudio.c		\
			    gstosxaudioelement.c	\
			    gstosxaudiosink.c		\
			    gstosxaudiosrc.c

# FIXME: clean up these linker flags
libgstosxaudio_la_CFLAGS = $(GST_CFLAGS)
libgstosxaudio_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la $(GST_LIBS)
LIBS = -Wl,-framework -Wl,CoreAudio
libgstosxaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -framework CoreAudio

noinst_HEADERS = gstosxaudiosink.h	\
		gstosxaudioelement.h	\
		gstosxaudiosrc.h



