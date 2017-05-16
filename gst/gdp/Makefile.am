plugin_LTLIBRARIES = libgstgdp.la

libgstgdp_la_SOURCES = \
	dataprotocol.c \
	gstgdp.c \
	gstgdppay.c \
	gstgdpdepay.c

libgstgdp_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgdp_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstgdp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	dataprotocol.h \
	dp-private.h   \
	gstgdppay.h    \
	gstgdpdepay.h
