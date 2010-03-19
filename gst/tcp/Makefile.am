plugin_LTLIBRARIES = libgsttcp.la

# variables used for enum/marshal generation
glib_enum_headers = gsttcp.h
glib_enum_define = GST_TCP
glib_gen_prefix = gst_tcp
glib_gen_basename = gsttcp

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gsttcp-enumtypes.c gsttcp-marshal.c
built_headers = gsttcp-enumtypes.h gsttcp-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgsttcp_la_SOURCES = \
	gsttcpplugin.c \
	gsttcp.c \
	gstmultifdsink.c  \
	gsttcpclientsrc.c gsttcpclientsink.c \
	gsttcpserversrc.c gsttcpserversink.c

nodist_libgsttcp_la_SOURCES = \
	$(built_sources)

# remove ENABLE_NEW when dataprotocol is stable
libgsttcp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_GDP_CFLAGS) $(GST_CFLAGS) -DGST_ENABLE_NEW
libgsttcp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttcp_la_LIBADD = $(GST_BASE_LIBS) $(GST_GDP_LIBS) $(GST_LIBS)
libgsttcp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
  gsttcpplugin.h \
  gsttcp.h \
  gstmultifdsink.h  \
  gsttcpclientsrc.h gsttcpclientsink.h \
  gsttcpserversrc.h gsttcpserversink.h

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gsttcp-marshal.list
