plugin_LTLIBRARIES = libgstacmmp3dec.la

libgstacmmp3dec_la_SOURCES = acmmp3dec.c
libgstacmmp3dec_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(ACMMP3DEC_CFLAGS)
libgstacmmp3dec_la_LIBADD =      \
    $(GST_BASE_LIBS)             \
    $(GST_PLUGINS_BASE_LIBS)     \
    -lgstriff-$(GST_MAJORMINOR)  \
    -lgstaudio-$(GST_MAJORMINOR) \
    -lgsttag-$(GST_MAJORMINOR)   \
    -lmsacm32
libgstacmmp3dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)
libgstacmmp3dec_la_LIBTOOLFLAGS = --tag=disable-static
