if USE_SOUNDTOUCH

GST_SOUNDTOUCH_TESTS = pitch-test

pitch_test_SOURCES = pitch-test.c
pitch_test_CFLAGS  = $(GST_CONTROLLER_CFLAGS)
pitch_test_LDADD   = $(GST_CONTROLLER_LIBS)
pitch_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

else
GST_SOUNDTOUCH_TESTS = 
endif

GST_VP8PARSER_TESTS     = vp8parser-test
vp8parser_test_SOURCES  = vp8parser-test.c
vp8parser_test_CFLAGS   = -I$(top_srcdir)/gst-libs $(GST_CFLAGS)
vp8parser_test_LDADD    = $(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la $(GST_LIBS)

# needs porting
#if HAVE_GTK
#
#GST_METADATA_TESTS = metadata_editor
#
#metadata_editor_SOURCES = metadata_editor.h metadata_editor.c
#metadata_editor_CFLAGS  = \
#	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS) $(GMODULE_EXPORT_CFLAGS)
#metadata_editor_LDADD   = \
#	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-0.10 $(GST_LIBS) $(GTK_LIBS) $(GMODULE_EXPORT_LIBS)
#else
GST_METADATA_TESTS =
#endif

noinst_PROGRAMS = $(GST_SOUNDTOUCH_TESTS) $(GST_METADATA_TESTS) $(GST_VP8PARSER_TESTS)

