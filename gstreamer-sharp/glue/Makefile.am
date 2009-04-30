lib_LTLIBRARIES = libgstreamersharpglue-0.10.la

libgstreamersharpglue_0_10_la_SOURCES =	\
	buffer.c	\
	caps.c          \
	clock.c		\
	clockentry.c	\
	message.c \
	event.c \
	bin.c \
	structure.c \
	taglist.c \
	task.c \
	object.c \
	pad.c \
	gobject.c \
	typefind.c \
	indexentry.c \
	indexfactory.c

nodist_libgstreamersharpglue_0_10_la_SOURCES = generated.c 

libgstreamersharpglue_0_10_la_LIBADD = $(GST_LIBS)

INCLUDES = -Wall -D_FORTIFY_SOURCE=2 $(GST_CFLAGS) -I$(top_srcdir)

CLEANFILES= lib*.a lib*.la
MAINTAINERCLEANFILES = Makefile.in

