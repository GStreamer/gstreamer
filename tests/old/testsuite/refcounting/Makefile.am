SUBDIRS =

testprogs = object element

object_SOURCES = object.c mem.c
element_SOURCES = element.c mem.c

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)
