filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstvideoscale.la

if HAVE_CPU_I386
ARCHSRCS = \
	videoscale_x86.c \
	videoscale_x86_asm.s
else
ARCHSRCS =
endif

libgstvideoscale_la_SOURCES = \
			gstvideoscale.c \
			videoscale.c \
			$(ARCHSRCS)
libvideoscale_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstvideoscale.h videoscale_x86.h

