plugin_LTLIBRARIES = libgstossaudio.la

libgstossaudio_la_SOURCES = gstossaudio.c	\
			    gstosselement.c	\
			    gstossmixer.c	\
			    gstosssink.c 	\
			    gstosssrc.c

libgstossaudio_la_CFLAGS = $(GST_CFLAGS)
libgstossaudio_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstosssink.h	\
		gstosssrc.h	\
		gstosselement.h\
		gstossmixer.h

noinst_PROGRAMS = oss_probe

oss_probe_SOURCES = oss_probe.c
oss_probe_CFLAGS = $(GST_CFLAGS)
oss_probe_LDADD = $(GLIB_LIBS)

