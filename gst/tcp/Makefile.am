plugin_LTLIBRARIES = libgsttcp.la

# variables used for enum/marshal generation
glib_enum_headers = gsttcp.h
glib_enum_define = GST_TCP_PROTOCOL
glib_enum_prefix = gst_tcp_protocol

include $(top_srcdir)/common/glib-gen.mak

built_sources = gsttcp-enumtypes.c
built_headers = gsttcp-enumtypes.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgsttcp_la_SOURCES = \
	gsttcpplugin.c \
	gsttcpsrc.c gsttcpsink.c \
	$(built_sources) \
	gsttcp.c \
	gsttcpclientsrc.c gsttcpclientsink.c \
	gsttcpserversrc.c gsttcpserversink.c

# remove ENABLE_NEW when dataprotocol is stable
libgsttcp_la_CFLAGS = $(GST_CFLAGS) -DGST_ENABLE_NEW
libgsttcp_la_LIBADD =
libgsttcp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
  $(built_headers) \
  gsttcpplugin.h \
  gsttcpsrc.h gsttcpsink.h \
  gsttcp.h \
  gsttcpclientsrc.h gsttcpclientsink.h \
  gsttcpserversrc.h gsttcpserversink.h

CLEANFILES = $(BUILT_SOURCES)
