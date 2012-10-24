plugin_LTLIBRARIES = libgstmplex.la

libgstmplex_la_SOURCES = \
	gstmplex.cc \
	gstmplexibitstream.cc \
	gstmplexjob.cc \
	gstmplexoutputstream.cc

libgstmplex_la_CXXFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(MPLEX_CFLAGS)
libgstmplex_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_LIBS) $(MPLEX_LIBS) 
libgstmplex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(MPLEX_LDFLAGS)
libgstmplex_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstmplex.hh \
	gstmplexibitstream.hh \
	gstmplexjob.hh \
	gstmplexoutputstream.hh
