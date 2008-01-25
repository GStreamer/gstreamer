if USE_SOUNDTOUCH

GST_SOUNDTOUCH_TESTS = pitch-test

pitch_test_SOURCES = pitch-test.c
pitch_test_CFLAGS  = $(GST_CONTROLLER_CFLAGS)
pitch_test_LDADD   = $(GST_CONTROLLER_LIBS)
pitch_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

else
GST_SOUNDTOUCH_TESTS = 
endif

if USE_METADATA

GST_METADATA_TESTS = metadata_editor

metadata_editor_SOURCES = metadata_editor.c
metadata_editor_CFLAGS  = $(GST_CFLAGS) \
		`pkg-config --cflags gstreamer-0.10 libglade-2.0 gtk+-2.0`
metadata_editor_LDADD   = $(GST_LIBS)
metadata_editor_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
		-Wl -export-dynamic \
		-lgstinterfaces-0.10 \
		`pkg-config --libs gstreamer-0.10 libglade-2.0 gtk+-2.0`

else
GST_METADATA_TESTS =
endif


equalizer_test_SOURCES = equalizer-test.c
equalizer_test_CFLAGS  = $(GST_CFLAGS)
equalizer_test_LDADD   = $(GST_LIBS)
equalizer_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = $(GST_SOUNDTOUCH_TESTS) $(GST_METADATA_TESTS) equalizer-test
