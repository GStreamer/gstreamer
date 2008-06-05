
noinst_PROGRAMS = appsrc_ex appsrc-stream

appsrc_ex_SOURCES = appsrc_ex.c
appsrc_ex_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
appsrc_ex_LDFLAGS = \
    $(GST_LIBS) \
    $(top_builddir)/gst-libs/gst/app/libgstapp-@GST_MAJORMINOR@.la

appsrc_stream_SOURCES = appsrc-stream.c
appsrc_stream_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
appsrc_stream_LDFLAGS = $(GST_LIBS)
