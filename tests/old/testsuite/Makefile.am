# thomas : removed refcounting for now until it is fixed
SUBDIRS = capsnego plugin bytestream

testprogs = test_gst_init

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)


DIST_SUBDIRS = refcounting capsnego plugin bytestream
