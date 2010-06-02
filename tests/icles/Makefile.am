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
if HAVE_GTK

GST_METADATA_TESTS = metadata_editor

metadata_editor_SOURCES = metadata_editor.h metadata_editor.c
metadata_editor_CFLAGS  = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS) $(GMODULE_EXPORT_CFLAGS)
metadata_editor_LDADD   = \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-0.10 $(GST_LIBS) $(GTK_LIBS) $(GMODULE_EXPORT_LIBS)
else
GST_METADATA_TESTS =
endif
else
GST_METADATA_TESTS =
endif

equalizer_test_SOURCES = equalizer-test.c
equalizer_test_CFLAGS  = $(GST_CFLAGS)
equalizer_test_LDADD   = $(GST_LIBS)
equalizer_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

output_selector_test_SOURCES = output-selector-test.c
output_selector_test_CFLAGS  = $(GST_CFLAGS)
output_selector_test_LDADD   = $(GST_LIBS)
output_selector_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


noinst_PROGRAMS = $(GST_SOUNDTOUCH_TESTS) $(GST_METADATA_TESTS) \
	equalizer-test output-selector-test

