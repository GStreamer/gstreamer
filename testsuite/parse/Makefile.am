testprogs = parse1 parse2

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

parse1_SOURCES = parse1.c
parse2_SOURCES = parse2.c

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
AM_CFLAGS = $(GST_CFLAGS)
