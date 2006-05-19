lib_LTLIBRARIES = libgstreamersharpglue-0.10.la

libgstreamersharpglue_0_10_la_SOURCES =		\
				clock.c		\
				message.c	\
				miniobject.c

nodist_libgstreamersharpglue_0_10_la_SOURCES = generated.c

libgstreamersharpglue_0_10_la_LIBADD = $(GST_LIBS)

INCLUDES = $(GST_CFLAGS) -I$(top_srcdir)

CLEANFILES= lib*.a lib*.la
MAINTAINERCLEANFILES = Makefile.in

