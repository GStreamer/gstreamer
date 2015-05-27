noinst_PROGRAMS = audio-example control-sources text-color-example absolute-example

AM_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs

LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)
