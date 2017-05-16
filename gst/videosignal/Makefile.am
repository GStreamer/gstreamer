plugin_LTLIBRARIES = libgstvideosignal.la 

libgstvideosignal_la_SOURCES = gstvideosignal.c   \
                               gstvideoanalyse.c \
                               gstvideoanalyse.h \
                               gstsimplevideomarkdetect.c \
                               gstsimplevideomarkdetect.h \
                               gstsimplevideomark.c \
                               gstsimplevideomark.h

libgstvideosignal_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideosignal_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideosignal_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
