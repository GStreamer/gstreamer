plugin_LTLIBRARIES = libgsttcp.la

if HAVE_SYS_SOCKET_H
multifdsink_SOURCES = \
	gstmultifdsink.c
else
multifdsink_SOURCES =
endif

libgsttcp_la_SOURCES = \
	gstsocketsrc.c \
	gsttcpplugin.c \
	gsttcpclientsrc.c gsttcpclientsink.c \
	$(multifdsink_SOURCES) \
	gstmultihandlesink.c  \
	gstmultisocketsink.c  \
	gsttcpserversrc.c gsttcpserversink.c

libgsttcp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_NET_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgsttcp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttcp_la_LIBADD = $(GST_BASE_LIBS) $(GST_NET_LIBS) $(GST_LIBS) $(GIO_LIBS)
libgsttcp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
  gstsocketsrc.h \
  gsttcp.h \
  gsttcpclientsrc.h gsttcpclientsink.h \
  gstmultifdsink.h  \
  gstmultisocketsink.h  \
  gsttcpserversrc.h gsttcpserversink.h gstmultihandlesink.h

CLEANFILES = $(BUILT_SOURCES)
