

plugin_LTLIBRARIES = libgstlinsys.la

libgstlinsys_la_SOURCES = \
	gstlinsyssdisink.c \
	gstlinsyssdisink.h \
	gstlinsyssdisrc.c \
	gstlinsyssdisrc.h \
	gstlinsys.c

libgstlinsys_la_CFLAGS = \
	-I$(srcdir)/include \
	$(EWGST_CFLAGS) \
	$(GST_BASE_PLUGINS_CFLAGS) \
	$(GST_CFLAGS)
libgstlinsys_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS) \
	$(GST_LDFLAGS)
libgstlinsys_la_LIBADD = $(GST_VIDEO_LIBS) $(GST_LIBS)

