# plugindir is set in configure

plugin_LTLIBRARIES = libgstasfmux.la

# sources used to compile this plug-in
libgstasfmux_la_SOURCES = gstasfmux.c \
                          gstasfobjects.c \
                          gstasfparse.c \
                          gstrtpasfpay.c \
                          gstasf.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstasfmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) 
libgstasfmux_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
                         -lgstrtp-@GST_API_VERSION@
libgstasfmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstasfmux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstasfmux.h \
                 gstasfobjects.h \
                 gstasfparse.h \
                 gstrtpasfpay.h
