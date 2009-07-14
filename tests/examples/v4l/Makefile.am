noinst_PROGRAMS = probe

probe_SOURCES = probe.c
probe_CFLAGS  = $(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) $(GST_CFLAGS)
probe_LDFLAGS = $(GST_PLUGINS_BASE_LIBS) \
		$(GST_BASE_LIBS) $(GST_LIBS) \
		$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la

