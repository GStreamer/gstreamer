# FIXME : refcounting threads bytestream
SUBDIRS = capsnego plugin cleanup

testprogs = test_gst_init

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GST_CFLAGS)

# FIXME : refcounting threadds
DIST_SUBDIRS = capsnego plugin bytestream cleanup
