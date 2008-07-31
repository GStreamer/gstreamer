if USE_X
X_TESTS = stress-xoverlay

stress_xoverlay_SOURCES = stress-xoverlay.c
stress_xoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS)
stress_xoverlay_LDFLAGS = $(GST_LIBS) $(X_LIBS) $(LIBM)
stress_xoverlay_LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la
else
X_TESTS =
endif

if USE_PANGO
PANGO_TESTS = test-textoverlay

test_textoverlay_SOURCES = test-textoverlay.c
test_textoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_textoverlay_LDFLAGS = $(GST_LIBS) $(LIBM)

else
PANGO_TESTS = 
endif

stress_playbin_SOURCES = stress-playbin.c
stress_playbin_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
stress_playbin_LDFLAGS = $(GST_LIBS) $(LIBM)

test_scale_SOURCES = test-scale.c
test_scale_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_scale_LDFLAGS = $(GST_LIBS) $(LIBM)

noinst_PROGRAMS = $(X_TESTS) $(PANGO_TESTS) stress-playbin test-scale
