testprogs = object element pad element_pad bin thread

object_SOURCES = object.c mem.c
element_SOURCES = element.c mem.c
pad_SOURCES = pad.c mem.c
element_pad_SOURCES = element_pad.c mem.c
bin_SOURCES = bin.c mem.c
thread_SOURCES = thread.c mem.c

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

noinst_HEADERS = mem.h

# we have nothing but apps here, we can do this safely
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)

