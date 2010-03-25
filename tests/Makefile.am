noinst_PROGRAMS = 	\
	test-display	\
	test-surfaces	\
	test-windows	\
	$(NULL)

noinst_PROGRAMS +=	\
	test-textures	\
	$(NULL)

TEST_CFLAGS = \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	$(X11_CFLAGS)

TEST_LIBS = \
	$(X11_LIBS)

TEST_GLX_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx-@GST_MAJORMINOR@.la

TEST_X11_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-@GST_MAJORMINOR@.la

test_display_SOURCES	= test-display.c
test_display_CFLAGS	= $(TEST_CFLAGS)
test_display_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_surfaces_SOURCES	= test-surfaces.c
test_surfaces_CFLAGS	= $(TEST_CFLAGS)
test_surfaces_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_windows_SOURCES	= test-windows.c
test_windows_CFLAGS	= $(TEST_CFLAGS)
test_windows_LDADD	= $(TEST_LIBS) $(TEST_X11_LIBS)

test_textures_SOURCES	= test-textures.c
test_textures_CFLAGS	= $(TEST_CFLAGS)
test_textures_LDADD	= $(TEST_LIBS) $(TEST_GLX_LIBS)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
