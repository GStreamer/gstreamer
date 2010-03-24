noinst_PROGRAMS = 	\
	test-display	\
	test-surfaces	\
	test-windows	\
	$(NULL)

TEST_CFLAGS = \
	$(LIBVA_EXTRA_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	$(X11_CFLAGS)

TEST_LIBS = \
	$(LIBVA_EXTRA_LIBS) \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-@GST_MAJORMINOR@.la \
	$(X11_LIBS)

test_display_SOURCES	= test-display.c
test_display_CFLAGS	= $(TEST_CFLAGS)
test_display_LDADD	= $(TEST_LIBS)

test_surfaces_SOURCES	= test-surfaces.c
test_surfaces_CFLAGS	= $(TEST_CFLAGS)
test_surfaces_LDADD	= $(TEST_LIBS)

test_windows_SOURCES	= test-windows.c
test_windows_CFLAGS	= $(TEST_CFLAGS)
test_windows_LDADD	= $(TEST_LIBS)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
