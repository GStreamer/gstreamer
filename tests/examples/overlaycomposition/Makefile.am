noinst_PROGRAMS = overlaycomposition

overlaycomposition_SOURCES = overlaycomposition.c
overlaycomposition_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
overlaycomposition_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_LIBS) $(GIO_LIBS) $(LIBM)
