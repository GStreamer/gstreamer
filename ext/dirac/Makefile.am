plugin_LTLIBRARIES = libgstdirac.la

libgstdirac_la_SOURCES = \
	gstdirac.cc \
	gstdiracenc.cc

libgstdirac_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(DIRAC_CFLAGS)
libgstdirac_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(DIRAC_LIBS) -lz $(LIBM)
libgstdirac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdirac_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstdiracdec.h

