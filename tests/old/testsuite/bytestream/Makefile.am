include ../Rules

tests_pass = filepadsink
tests_fail = test1 
tests_ignore =

test1_SOURCES = test1.c gstbstest.c
test1_LDFLAGS = $(top_builddir)/libs/gst/bytestream/libgstbytestream.la

filepadsink_LDFLAGS = $(top_builddir)/libs/gst/bytestream/libgstbytestream.la
