
plugin_LTLIBRARIES = libgstdirac.la

libgstdirac_la_SOURCES = gstdirac.cc gstdiracdec.cc
#gstdiracenc.c
libgstdirac_la_CXXFLAGS = $(GST_CFLAGS) $(DIRAC_CFLAGS)
libgstdirac_la_LIBADD = $(DIRAC_LIBS) 
libgstdirac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

