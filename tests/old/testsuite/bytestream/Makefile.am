## filterdir = $(libdir)/gst

testprogs = test1 

check_PROGRAMS = $(testprogs)

test1_SOURCES = test1.c gstbstest.c mem.c
test1_LDFLAGS = $(GST_LIBS) -L$(top_builddir)/libs/gst/bytestream/ -lgstbytestream -L$(top_builddir)/gst/schedulers/ -lgstbasicscheduler
test1_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = mem.h

# we have nothing but apps here, we can do this safely
# LIBS = $(GST_LIBS)
