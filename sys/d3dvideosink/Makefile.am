plugin_LTLIBRARIES = libgstd3d.la

libgstd3d_la_SOURCES = d3dvideosink.c d3dhelpers.c
libgstd3d_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(DIRECT3D_CFLAGS) $(DIRECTX_CFLAGS) \
	-DDISABLE_BUFFER_POOL
libgstd3d_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(DIRECT3D_LIBS) $(DIRECTX_LDFAGS)
libgstd3d_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = d3dvideosink.h d3dhelpers.h
