# GstObject is no longer instantiatable in glib2
# neither is GstElement
testprogs = pad bin 

# element_SOURCES = element.c mem.c
pad_SOURCES = pad.c mem.c
# element_pad_SOURCES = element_pad.c mem.c
bin_SOURCES = bin.c mem.c

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

EXTRA_DIST = thread.c object.c element.c element_pad.c

noinst_HEADERS = mem.h

# we have nothing but apps here, we can do this safely
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)

