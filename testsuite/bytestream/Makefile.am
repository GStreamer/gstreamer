include ../Rules

tests_pass = filepadsink
tests_fail = 
tests_ignore =

filepadsink_CFLAGS = $(AM_CFLAGS) -DTHE_FILE=\""$(top_srcdir)/configure.ac"\"
filepadsink_LDFLAGS = $(top_builddir)/libs/gst/bytestream/libgstbytestream.la
