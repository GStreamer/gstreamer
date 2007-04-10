plugin_LTLIBRARIES = libgstrtpmanager.la

glib_enum_define = GST_RTP_BIN
glib_enum_prefix = gst_rtp_bin

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstrtpbin-marshal.c
built_headers = gstrtpbin-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgstrtpmanager_la_SOURCES = gstrtpmanager.c \
			      gstrtpbin.c \
			      gstrtpclient.c \
			      async_jitter_queue.c \
			      gstrtpjitterbuffer.c \
			      gstrtpptdemux.c \
			      gstrtpssrcdemux.c \
			      gstrtpsession.c

nodist_libgstrtpmanager_la_SOURCES = \
      $(built_sources)

noinst_HEADERS = gstrtpbin.h \
		 gstrtpclient.h \
                 async_jitter_queue.h \
		 gstrtpjitterbuffer.h \
                 gstrtpptdemux.h \
                 gstrtpssrcdemux.h \
		 gstrtpsession.h

libgstrtpmanager_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS)
libgstrtpmanager_la_LIBADD = $(GST_LIBS_LIBS)
libgstrtpmanager_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstrtpbin-marshal.list


