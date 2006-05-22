plugin_LTLIBRARIES = libgstgdp.la

libgstgdp_la_SOURCES = \
	gstgdp.c \
	gstgdppay.c \
	gstgdpdepay.c

libgstgdp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgdp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_GDP_LIBS)

noinst_HEADERS =			\
		 gstgdppay.h		\
		 gstgdpdepay.h
