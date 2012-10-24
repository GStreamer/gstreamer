plugin_LTLIBRARIES = libgstossaudio.la

libgstossaudio_la_SOURCES = gstossaudio.c	 \
		            gstosshelper.c	 \
			    gstosssink.c	 \
			    gstosssrc.c

libgstossaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstossaudio_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstossaudio_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = common.h            \
		gstosssink.h	     \
		gstosssrc.h	     \
                gstosshelper.h	     \
                gstossdmabuffer.h
