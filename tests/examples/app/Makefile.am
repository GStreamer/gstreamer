
noinst_PROGRAMS = appsrc_ex appsrc-stream appsrc-stream2 appsrc-ra \
		  appsrc-seekable appsink-src appsink-src2

appsrc_ex_SOURCES = appsrc_ex.c
appsrc_ex_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsrc_ex_LDADD = \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS)

appsrc_stream_SOURCES = appsrc-stream.c
appsrc_stream_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsrc_stream_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

appsrc_stream2_SOURCES = appsrc-stream2.c
appsrc_stream2_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsrc_stream2_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

appsrc_ra_SOURCES = appsrc-ra.c
appsrc_ra_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsrc_ra_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

appsrc_seekable_SOURCES = appsrc-seekable.c
appsrc_seekable_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsrc_seekable_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

appsink_src_SOURCES = appsink-src.c
appsink_src_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsink_src_LDADD = \
    $(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
    $(GST_BASE_LIBS) $(GST_LIBS)

appsink_src2_SOURCES = appsink-src2.c
appsink_src2_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
appsink_src2_LDADD = \
    $(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
    $(GST_BASE_LIBS) $(GST_LIBS)
