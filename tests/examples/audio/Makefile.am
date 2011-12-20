if HAVE_GTK
GTK_EXAMPLES = audiomix volume
else
GTK_EXAMPLES =
endif

noinst_PROGRAMS = $(GTK_EXAMPLES)

if HAVE_GTK
audiomix_SOURCES = audiomix.c
audiomix_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) -D_GNU_SOURCE
audiomix_LDADD = $(GST_LIBS) $(GTK_LIBS) $(LIBM)

volume_SOURCES = volume.c
volume_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) -D_GNU_SOURCE
volume_LDADD = $(GST_LIBS) $(GTK_LIBS) $(LIBM)
endif
