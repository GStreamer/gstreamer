plugin_LTLIBRARIES = libgstdirac.la

libgstdirac_la_SOURCES = \
	gstdirac.cc \
	gstdiracdec.cc

libgstdirac_la_CXXFLAGS = $(GST_CFLAGS) $(DIRAC_CFLAGS)
libgstdirac_la_LIBADD = $(DIRAC_LIBS) -lz -lm
libgstdirac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstdiracdec.h
