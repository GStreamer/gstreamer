if HAVE_GTK
noinst_PROGRAMS = volume
volume_SOURCES = volume.c
volume_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) -D_GNU_SOURCE
volume_LDADD = $(GST_LIBS) $(GTK_LIBS) $(LIBM)
endif
