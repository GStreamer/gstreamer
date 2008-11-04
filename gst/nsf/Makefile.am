
plugin_LTLIBRARIES = libgstnsf.la

NOSEFART_SOURCES=fmopl.c \
	log.c \
	mmc5_snd.c \
	nes_apu.c \
	vrc7_snd.c \
	fds_snd.c \
	nes6502.c \
	nsf.c \
	vrcvisnd.c

NOSEFART_INCLUDES=fmopl.h \
	log.h \
	mmc5_snd.h \
	dis6502.h \
	nes_apu.h \
	osd.h \
	vrc7_snd.h \
	fds_snd.h \
	nes6502.h \
	nsf.h \
	types.h \
	vrcvisnd.h

libgstnsf_la_SOURCES = gstnsf.c $(NOSEFART_SOURCES)

libgstnsf_la_CFLAGS = $(GST_CFLAGS) -DNSF_PLAYER
libgstnsf_la_LIBADD = $(GST_LIBS) $(LIBM)
libgstnsf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnsf_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstnsf.h $(NOSEFART_INCLUDES)

EXTRA_DIST =
