plugin_LTLIBRARIES = libgstscaletempoplugin.la

# sources used to compile this plug-in
libgstscaletempoplugin_la_SOURCES = gstscaletempoplugin.c gstscaletempo.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstscaletempoplugin_la_CFLAGS = $(GST_CFLAGS)
libgstscaletempoplugin_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstscaletempoplugin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstscaletempoplugin_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstscaletempo.h
