
plugin_LTLIBRARIES =	libgstmythtvsrc.la

libgstmythtvsrc_la_SOURCES =	\
		gstmythtvsrc.c

libgstmythtvsrc_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GMYTH_CFLAGS)

libgstmythtvsrc_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS)
libgstmythtvsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstmythtvsrc_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GMYTH_LIBS)

noinst_HEADERS = \
	gstmythtvsrc.h

