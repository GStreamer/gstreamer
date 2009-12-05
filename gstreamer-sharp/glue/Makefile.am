lib_LTLIBRARIES = libgstreamersharpglue-0.10.la

libgstreamersharpglue_0_10_la_SOURCES =	\
	glib.c		\
	buffer.c	\
	caps.c          \
	clock.c		\
	clockentry.c	\
	message.c \
	event.c \
	structure.c \
	task.c \
	object.c \
	pad.c \
	gobject.c \
	typefind.c \
	indexentry.c \
	indexfactory.c \
	mixertrack.c \
	tunernorm.c \
	adapter.c \
	controller.c \
	controlsource.c \
	videoutil.c

nodist_libgstreamersharpglue_0_10_la_SOURCES = generated.c 

libgstreamersharpglue_0_10_la_LIBADD = $(GST_LIBS)

libgstreamersharpglue_0_10_la_LDFLAGS = -module -avoid-version -no-undefined

libgstreamersharpglue.dll: $(libgstreamersharpglue_0_10_la_OBJECTS) libgstreamersharpglue.rc libgstreamersharpglue.def
	./build-dll libgstreamersharpglue-0.10 $(VERSION)

INCLUDES = -Wall $(GST_CFLAGS) -I$(top_srcdir)

CLEANFILES= lib*.a lib*.la
MAINTAINERCLEANFILES = Makefile.in

