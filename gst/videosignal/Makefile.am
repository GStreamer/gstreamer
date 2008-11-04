plugin_LTLIBRARIES = libgstvideosignal.la 

noinst_HEADERS = gstvideoanalyse.h gstvideodetect.h gstvideomark.h

libgstvideosignal_la_SOURCES = gstvideosignal.c   \
                               gstvideoanalyse.c  \
                               gstvideodetect.c   \
                               gstvideomark.c

libgstvideosignal_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstvideosignal_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideosignal_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideosignal_la_LIBTOOLFLAGS = --tag=disable-static

