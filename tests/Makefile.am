noinst_PROGRAMS = 	\
	test-decode	\
	test-display	\
	test-surfaces	\
	test-windows	\
	$(NULL)

if USE_GLX
noinst_PROGRAMS +=	\
	test-textures	\
	$(NULL)
endif

TEST_CFLAGS		= $(LIBVA_CFLAGS) -I$(top_srcdir)/gst-libs $(GST_CFLAGS)
TEST_X11_CFLAGS		= -DUSE_X11 $(X11_CFLAGS)
TEST_GLX_CFLAGS		= -DUSE_GLX $(GLX_CFLAGS)
TEST_MIX_CFLAGS		= $(TEST_X11_CFLAGS)
if USE_GLX
TEST_MIX_CFLAGS		+= $(TEST_GLX_CFLAGS)
endif

TEST_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-@GST_MAJORMINOR@.la

TEST_X11_LIBS = \
	$(X11_LIBS) \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-@GST_MAJORMINOR@.la

TEST_GLX_LIBS = \
	$(GLX_LIBS) \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx-@GST_MAJORMINOR@.la

TEST_MIX_LIBS		= $(TEST_X11_LIBS)
if USE_GLX
TEST_MIX_LIBS		+= $(TEST_GLX_LIBS)
endif

test_codecs_source_c	= test-mpeg2.c test-h264.c test-vc1.c
test_codecs_source_h	= $(test_codecs_source_c:%.c=%.h)

test_decode_SOURCES	= test-decode.c $(test_codecs_source_c)
test_decode_CFLAGS	= $(TEST_CFLAGS) $(TEST_X11_CFLAGS)
test_decode_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_display_SOURCES	= test-display.c
test_display_CFLAGS	= $(TEST_CFLAGS) $(TEST_MIX_CFLAGS)
test_display_LDADD	= $(TEST_LIBS) $(TEST_MIX_LIBS)

test_surfaces_SOURCES	= test-surfaces.c
test_surfaces_CFLAGS	= $(TEST_CFLAGS) $(TEST_X11_CFLAGS)
test_surfaces_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_windows_SOURCES	= test-windows.c image.c
test_windows_CFLAGS	= $(TEST_CFLAGS) $(TEST_X11_CFLAGS)
test_windows_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_textures_SOURCES	= test-textures.c image.c
test_textures_CFLAGS	= $(TEST_CFLAGS) $(TEST_GLX_CFLAGS)
test_textures_LDADD	= $(TEST_LIBS) $(TEST_GLX_LIBS)

EXTRA_DIST = \
	image.h			\
	test-decode.h 		\
	$(test_codecs_source_c)	\
	$(test_codecs_source_h)	\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
