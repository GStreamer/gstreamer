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
			      gstrtpjitterbuffer.c \
			      gstrtpptdemux.c \
			      gstrtpssrcdemux.c \
			      rtpjitterbuffer.c      \
			      rtpsession.c      \
			      rtpsource.c      \
			      rtpstats.c      \
			      gstrtpsession.c

nodist_libgstrtpmanager_la_SOURCES = \
      $(built_sources)

noinst_HEADERS = gstrtpbin.h \
		 gstrtpclient.h \
		 gstrtpjitterbuffer.h \
                 gstrtpptdemux.h \
                 gstrtpssrcdemux.h \
                 rtpjitterbuffer.h \
		 rtpsession.h  \
		 rtpsource.h  \
		 rtpstats.h  \
		 gstrtpsession.h

libgstrtpmanager_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(ERROR_CFLAGS)
libgstrtpmanager_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstnetbuffer-@GST_MAJORMINOR@ -lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS_LIBS)
libgstrtpmanager_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstrtpbin-marshal.list


