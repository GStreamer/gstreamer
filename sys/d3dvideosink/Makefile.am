plugin_LTLIBRARIES = libgstd3dvideosink.la

libgstd3dvideosink_la_SOURCES = d3dvideosink.c directx/d3d.c directx/dx.c \
	directx/directx9/dx9.c directx/directx9/dx9_d3d.c \
	directx/directx10/dx10.c directx/directx10/dx10_d3d.c \
	directx/directx11/dx11.c directx/directx11/dx11_d3d.c
libgstd3dvideosink_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
libgstd3dvideosink_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(DIRECT3D_LIBS)
libgstd3dvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstd3dvideosink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = d3dvideosink.h directx/d3d.h directx/dx.h directx/directx.h \
	directx/directx9/dx9.h directx/directx9/dx9_d3d.h \
	directx/directx10/dx10.h directx/directx10/dx10_d3d.h \
	directx/directx11/dx11.h directx/directx11/dx11_d3d.h
