# plugindir is set in configure

plugin_LTLIBRARIES = libgstqtmux.la

# sources used to compile this plug-in
libgstqtmux_la_SOURCES = gstqtmux.c \
			atoms.c \
			descriptors.c \
			properties.c \
			gstqtmuxmap.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstqtmux_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstqtmux_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstqtmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstqtmux.h \
		atoms.h \
		descriptors.h \
		properties.h \
		fourcc.h \
		ftypcc.h \
		gstqtmuxmap.h
