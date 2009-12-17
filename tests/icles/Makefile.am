if USE_OSS4
OSS4_TESTS=test-oss4
else
OSS4_TESTS=
endif

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
if HAVE_GLADE
if HAVE_GTK

GST_METADATA_TESTS = metadata_editor

metadata_editor_SOURCES = metadata_editor.c
metadata_editor_CFLAGS  = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GLADE_CFLAGS)
metadata_editor_LDADD   = \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-0.10 $(GST_LIBS) \
	$(GLADE_LIBS)
else
GST_METADATA_TESTS =
endif
else
GST_METADATA_TESTS =
endif
else
GST_METADATA_TESTS =
endif

if USE_COG
COG_TESTS = cog-test
else
COG_TESTS =
endif

if USE_COG
CLEANFILES = cog-test.c

cog-test.c: $(srcdir)/../../ext/cog/cog.orc
	$(ORCC) --test -o cog-test.c $(srcdir)/../../ext/cog/cog.orc

nodist_cog_test_SOURCES = cog-test.c
cog_test_CFLAGS = $(COG_CFLAGS)
cog_test_LDADD = -lorc-test-0.4 $(COG_LIBS)
endif

equalizer_test_SOURCES = equalizer-test.c
equalizer_test_CFLAGS  = $(GST_CFLAGS)
equalizer_test_LDADD   = $(GST_LIBS)
equalizer_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

test_oss4_SOURCES = test-oss4.c
test_oss4_CFLAGS  = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
test_oss4_LDADD   = $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-0.10 $(GST_LIBS)
test_oss4_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

output_selector_test_SOURCES = output-selector-test.c
output_selector_test_CFLAGS  = $(GST_CFLAGS)
output_selector_test_LDADD   = $(GST_LIBS)
output_selector_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


noinst_PROGRAMS = $(GST_SOUNDTOUCH_TESTS) $(GST_METADATA_TESTS) $(OSS4_TESTS) \
	$(COG_TESTS) \
	equalizer-test output-selector-test

