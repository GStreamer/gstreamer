plugin_LTLIBRARIES = libgstossaudio.la

libgstossaudio_la_SOURCES = gstossaudio.c	 \
		            gstosshelper.c	 \
			    gstossmixer.c	 \
			    gstossmixerelement.c \
			    gstossmixertrack.c	 \
			    gstosssink.c	 \
			    gstosssrc.c

#		            gstossdmabuffer.c

libgstossaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstossaudio_la_LIBADD = \
        -lgstinterfaces-@GST_MAJORMINOR@ \
	-lgstaudio-@GST_MAJORMINOR@ \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS)
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = common.h            \
		gstosssink.h	     \
		gstosssrc.h	     \
                gstosshelper.h	     \
		gstossdmabuffer.h    \
		gstossmixer.h        \
		gstossmixerelement.h \
		gstossmixertrack.h

# noinst_PROGRAMS = #oss_probe

# oss_probe_SOURCES = oss_probe.c
# oss_probe_CFLAGS = $(GST_CFLAGS)
# oss_probe_LDADD = $(GLIB_LIBS)

