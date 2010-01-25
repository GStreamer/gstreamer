noinst_PROGRAMS = 	\
	test-display	\
	$(NULL)

TEST_CFLAGS = \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	$(X11_CFLAGS)

TEST_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-@GST_MAJORMINOR@.la \
	$(X11_LIBS)

test_display_SOURCES	= test-display.c
test_display_CFLAGS	= $(TEST_CFLAGS)
test_display_LDADD	= $(TEST_LIBS)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
