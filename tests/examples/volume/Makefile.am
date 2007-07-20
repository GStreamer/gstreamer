if HAVE_GTK
noinst_PROGRAMS = volume
volume_SOURCES = volume.c
volume_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS) -D_GNU_SOURCE
volume_LDFLAGS = $(GTK_LIBS) $(GST_LIBS) $(LIBM)
endif
