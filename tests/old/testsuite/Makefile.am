# FIXME : refcounting threads
SUBDIRS = capsnego plugin bytestream

testprogs = test_gst_init

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

# FIXME : refcounting threadds
DIST_SUBDIRS = capsnego plugin bytestream
