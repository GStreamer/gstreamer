# FIXME : refcounting threads bytestream cleanup
SUBDIRS = caps plugin elements clock ## dynparams

testprogs = test_gst_init

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GST_CFLAGS)

# FIXME : refcounting threadds
DIST_SUBDIRS = caps plugin bytestream cleanup elements dynparams clock
