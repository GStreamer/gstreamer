TEST_UVCH264_GLADE_FILES = window.glade \
			boolean_property.glade \
			enum_property.glade \
			int_property.glade

if HAVE_GTK

TEST_UVCH264_EXAMPLES = test-uvch264

test_uvch264_SOURCES = test-uvch264.c
test_uvch264_CFLAGS  = \
        $(GST_PLUGINS_BAD_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GTK_CFLAGS) \
	$(GMODULE_EXPORT_CFLAGS) \
        -DGST_USE_UNSTABLE_API
test_uvch264_LDADD   = \
        $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
        $(GST_LIBS) \
	$(GTK_LIBS) \
	$(GMODULE_EXPORT_LIBS)

noinst_DATA = $(TEST_UVCH264_GLADE_FILES)

else
TEST_UVCH264_EXAMPLES =
endif

noinst_PROGRAMS = $(TEST_UVCH264_EXAMPLES)

EXTRA_DIST = $(TEST_UVCH264_GLADE_FILES)

