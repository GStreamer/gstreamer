# plugindir is set in configure

plugin_LTLIBRARIES = libgstdccp.la

# sources used to compile this plug-in
libgstdccp_la_SOURCES = gstdccpplugin.c \
			gstdccp.c \
			gstdccpserversink.c \
			gstdccpserversrc.c \
			gstdccpclientsink.c \
			gstdccpclientsrc.c

# flags used to compile this plugin
libgstdccp_la_CFLAGS = $(GST_CFLAGS)
libgstdccp_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(DCCP_LIBS) $(WINSOCK2_LIBS)
libgstdccp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdccp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstdccp.h \
                 gstdccpserversink.h \
                 gstdccpserversrc.h \
                 gstdccpclientsink.h \
                 gstdccpclientsrc.h \
                 gstdccp_common.h
