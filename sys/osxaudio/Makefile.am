plugin_LTLIBRARIES = libgstosxaudio.la

libgstosxaudio_la_SOURCES = gstosxaudio.c	\
			    gstosxaudioelement.c	\
			    gstosxaudiosink.c		\
			    gstosxaudiosrc.c

libgstosxaudio_la_CFLAGS = $(GST_CFLAGS)
libgstosxaudio_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstosxaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -framework CoreAudio

noinst_HEADERS = gstosxaudiosink.h	\
		gstosxaudioelement.h	\
		gstosxaudiosrc.h



