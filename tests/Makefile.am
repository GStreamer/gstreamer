noinst_PROGRAMS = \
	simple-decoder			\
	test-decode			\
	test-display			\
	test-surfaces			\
	test-windows			\
	test-subpicture			\
	$(NULL)

if USE_GLX
noinst_PROGRAMS += \
	test-textures			\
	$(NULL)
endif

TEST_CFLAGS = \
	-DGST_USE_UNSTABLE_API		\
	-I$(top_srcdir)/gst-libs	\
	-I$(top_builddir)/gst-libs	\
	$(LIBVA_CFLAGS)			\
	$(GST_CFLAGS)			\
	$(NULL)

TEST_LIBS = \
	$(LIBVA_LIBS)			\
	$(GST_LIBS)			\
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-$(GST_API_VERSION).la

if USE_DRM
TEST_CFLAGS	+= $(LIBVA_DRM_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_DRM_LIBS)		\
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-drm-$(GST_API_VERSION).la
endif

if USE_X11
TEST_CFLAGS	+= $(X11_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_X11_LIBS)		\
	$(X11_LIBS)			\
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-$(GST_API_VERSION).la
endif

if USE_GLX
TEST_CFLAGS	+= $(X11_CFLAGS) $(GL_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_GLX_LIBS)		\
	$(X11_LIBS)			\
	$(GL_LIBS)			\
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx-$(GST_API_VERSION).la
endif

if USE_WAYLAND
TEST_CFLAGS	+= $(WAYLAND_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_WAYLAND_LIBS)		\
	$(WAYLAND_LIBS)			\
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-wayland-$(GST_API_VERSION).la
endif

test_utils_dec_source_c =	\
	decoder.c	\
	test-h264.c	\
	test-jpeg.c	\
	test-mpeg2.c	\
	test-mpeg4.c	\
	test-vc1.c	\
	$(NULL)
test_utils_dec_source_h	= $(test_utils_dec_source_c:%.c=%.h) test-decode.h

test_utils_source_c	= codec.c image.c output.c
test_utils_source_h	= codec.h image.h output.h

noinst_LTLIBRARIES	= libutils.la libutils_dec.la
libutils_la_SOURCES	= $(test_utils_source_c)
libutils_la_CFLAGS	= $(TEST_CFLAGS)
libutils_dec_la_SOURCES	= $(test_utils_dec_source_c)
libutils_dec_la_CFLAGS	= $(TEST_CFLAGS)

test_decode_SOURCES	= test-decode.c
test_decode_CFLAGS	= $(TEST_CFLAGS)
test_decode_LDADD	= libutils.la libutils_dec.la $(TEST_LIBS)

test_display_SOURCES	= test-display.c
test_display_CFLAGS	= $(TEST_CFLAGS)
test_display_LDADD	= libutils.la $(TEST_LIBS)

test_surfaces_SOURCES	= test-surfaces.c
test_surfaces_CFLAGS	= $(TEST_CFLAGS)
test_surfaces_LDADD	= libutils.la $(TEST_LIBS)

test_subpicture_SOURCES = test-subpicture.c test-subpicture-data.c
test_subpicture_CFLAGS  = $(TEST_CFLAGS) $(GST_VIDEO_CFLAGS)
test_subpicture_LDADD   = libutils.la libutils_dec.la $(TEST_LIBS) \
	$(GST_VIDEO_LIBS)

test_windows_SOURCES	= test-windows.c
test_windows_CFLAGS	= $(TEST_CFLAGS)
test_windows_LDADD	= libutils.la $(TEST_LIBS)

test_textures_SOURCES	= test-textures.c
test_textures_CFLAGS	= $(TEST_CFLAGS)
test_textures_LDADD	= libutils.la $(TEST_LIBS)

simple_decoder_source_c	= simple-decoder.c
simple_decoder_source_h	=
simple_decoder_SOURCES	= $(simple_decoder_source_c)
simple_decoder_CFLAGS	= $(TEST_CFLAGS) $(GST_VIDEO_CFLAGS)
simple_decoder_LDADD	= libutils.la $(TEST_LIBS) $(GST_VIDEO_LIBS) \
	$(top_builddir)/gst-libs/gst/video/libgstvaapi-videoutils.la

EXTRA_DIST = \
	test-subpicture-data.h		\
	$(simple_decoder_source_h)	\
	$(test_utils_dec_source_h)	\
	$(test_utils_source_h)		\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
