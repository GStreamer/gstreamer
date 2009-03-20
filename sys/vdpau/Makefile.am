plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpaudecoder.c\
	gstvdpauh264decoder.c

libgstvdpau_la_CFLAGS = $(GST_CFLAGS) $(X11_CFLAGS) -Ivdpau
libgstvdpau_la_LIBADD = $(GST_LIBS) $(X11_LIBS) -lvdpau
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpaudecoder.h\
	vdpauvariables.h \
	gstvdpauh264decoder.h
	

