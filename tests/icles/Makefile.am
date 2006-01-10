if USE_X
X_TESTS = stress-xoverlay

stress_xoverlay_SOURCES = stress-xoverlay.c
stress_xoverlay_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
stress_xoverlay_LDFLAGS = $(GST_LIBS) $(X_LIBS)
stress_xoverlay_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la
else
X_TESTS =
endif

noinst_PROGRAMS = $(X_TESTS)

