
plugin_LTLIBRARIES = libgstrtpmanager.la

libgstrtpmanager_la_SOURCES = gstrtpmanager.c \
			      gstrtpbin.c \
			      gstrtpclient.c \
			      async_jitter_queue.c \
			      gstrtpjitterbuffer.c \
			      gstrtpptdemux.c \
			      gstrtpsession.c

noinst_HEADERS = gstrtpbin.h \
		 gstrtpclient.h \
                 async_jitter_queue.h \
		 gstrtpjitterbuffer.h \
		 gstrtpsession.h

libgstrtpmanager_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS)
libgstrtpmanager_la_LIBADD = $(GST_LIBS_LIBS)
libgstrtpmanager_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@

EXTRA_DIST = 

