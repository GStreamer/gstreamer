
plugin_LTLIBRARIES = libgstossaudio.la

EXTRA_LTLIBRARIES = libgstosshelper.la

libgstossaudio_la_SOURCES = gstossaudio.c	\
			    gstosselement.c	\
			    gstossmixer.c	\
			    gstosssink.c 	\
			    gstosssrc.c  	\
			    gstossgst.c

libgstossaudio_la_CFLAGS = $(GST_CFLAGS)
libgstossaudio_la_LIBADD = $(top_builddir)/gst-libs/gst/mixer/libgstmixer.la
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstosshelper_la_SOURCES = gstosshelper.c
libgstosshelper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstosssink.h	\
		gstosssrc.h	\
		gstossgst.h	\
		gstosshelper.h	\
		gstosselement.h\
		gstossmixer.h
