TEST_AUDIOMIXMATRIX_EXAMPLES = test-audiomixmatrix

test_audiomixmatrix_SOURCES = test-audiomixmatrix.c
test_audiomixmatrix_CFLAGS  = \
        $(GST_PLUGINS_BAD_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GMODULE_EXPORT_CFLAGS) \
        -DGST_USE_UNSTABLE_API
test_audiomixmatrix_LDADD   = \
        $(GST_PLUGINS_BASE_LIBS) \
        $(GST_LIBS) \
	$(GMODULE_EXPORT_LIBS)

noinst_PROGRAMS = $(TEST_AUDIOMIXMATRIX_EXAMPLES)

