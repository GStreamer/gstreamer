plugin_LTLIBRARIES = libgstacmmp3dec.la

libgstacmmp3dec_la_SOURCES = acmmp3dec.c
libgstacmmp3dec_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(ACMMP3DEC_CFLAGS)
libgstacmmp3dec_la_LIBADD =      \
    $(GST_BASE_LIBS)             \
    $(GST_PLUGINS_BASE_LIBS)     \
    -lgstriff-$(GST_API_VERSION)  \
    -lgstaudio-$(GST_API_VERSION) \
    -lgsttag-$(GST_API_VERSION)   \
    -lmsacm32
libgstacmmp3dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)
