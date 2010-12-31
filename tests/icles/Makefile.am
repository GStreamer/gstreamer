SUBDIRS = playback
DIST_SUBDIRS = playback

if USE_X
X_TESTS = stress-xoverlay

stress_xoverlay_SOURCES = stress-xoverlay.c
stress_xoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS)
stress_xoverlay_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la

if HAVE_GTK_X11
X_TESTS += test-colorkey test-xoverlay

test_colorkey_SOURCES = test-colorkey.c
test_colorkey_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS) $(GTK_CFLAGS)
test_colorkey_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) $(GTK_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la

test_xoverlay_SOURCES = test-xoverlay.c
test_xoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS) $(GTK_CFLAGS)
test_xoverlay_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) $(GTK_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la
endif

output_selector_test_SOURCES = output-selector-test.c
output_selector_test_CFLAGS  = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
output_selector_test_LDADD   = $(GST_LIBS)
output_selector_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

input_selector_test_SOURCES = input-selector-test.c
input_selector_test_CFLAGS  = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
input_selector_test_LDADD   = $(GST_LIBS)
input_selector_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

X_TESTS += output-selector-test input-selector-test

else
X_TESTS =
endif

if USE_PANGO
PANGO_TESTS = test-textoverlay

test_textoverlay_SOURCES = test-textoverlay.c
test_textoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_textoverlay_LDADD = $(GST_LIBS) $(LIBM)

else
PANGO_TESTS = 
endif

audio_trickplay_SOURCES = audio-trickplay.c
audio_trickplay_CFLAGS  = $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
audio_trickplay_LDADD = $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(LIBM)

playbin_text_SOURCES = playbin-text.c
playbin_text_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
playbin_text_LDADD = $(GST_LIBS) $(LIBM)

position_formats_SOURCES = position-formats.c
position_formats_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
position_formats_LDADD = $(GST_LIBS) $(LIBM)

stress_playbin_SOURCES = stress-playbin.c
stress_playbin_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
stress_playbin_LDADD = $(GST_LIBS) $(LIBM)

test_scale_SOURCES = test-scale.c
test_scale_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_scale_LDADD = $(GST_LIBS) $(LIBM)

test_box_SOURCES = test-box.c
test_box_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_box_LDADD = $(GST_LIBS) $(LIBM)

noinst_PROGRAMS = $(X_TESTS) $(PANGO_TESTS) \
	audio-trickplay playbin-text position-formats stress-playbin \
	test-scale test-box
