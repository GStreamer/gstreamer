plugin_LTLIBRARIES = libgstossaudio.la

libgstossaudio_la_SOURCES = gstossaudio.c	\
		            gstosselement.c	\
		            gstosshelper.c	\
			    gstossmixer.c	\
			    gstosssink.c

#		            gstossdmabuffer.c

libgstossaudio_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstossaudio_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
                -lgstinterfaces-@GST_MAJORMINOR@ \
		-lgstaudio-@GST_MAJORMINOR@
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = gstosssink.h	  \
		gstosssrc.h	  \
		gstosselement.h   \
                gstosshelper.h	  \
		gstossdmabuffer.h \
		gstossmixer.h

# noinst_PROGRAMS = #oss_probe

# oss_probe_SOURCES = oss_probe.c
# oss_probe_CFLAGS = $(GST_CFLAGS)
# oss_probe_LDADD = $(GLIB_LIBS)

