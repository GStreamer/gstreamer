librarydir = $(libdir)

play_headers = \
	play.h

built_headers = \
	playenumtypes.h

library_LTLIBRARIES = libgstplay-@GST_MAJORMINOR@.la

libgstplay_@GST_MAJORMINOR@_la_SOURCES = play.c playenumtypes.c

libgstplay_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/play
libgstplay_@GST_MAJORMINOR@include_HEADERS = $(play_headers) $(built_headers)

libgstplay_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_CFLAGS)
libgstplay_@GST_MAJORMINOR@_la_LIBADD =	
	$(GST_LIBS) $(GST_PLUGINS_LIBS) \
	$(GST_CONTROL_LIBS) \
	 $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstplay_@GST_MAJORMINOR@_la_LDFLAGS = \
		-version-info @GST_PLUGINS_LIBVERSION@

BUILT_SOURCES = \
	playenumtypes.c \
	playenumtypes.h

CLEANFILES = $(BUILT_SOURCES)

playenumtypes.h: $(play_headers)
	glib-mkenums \
	--fhead "#ifndef __GST_PLAY_ENUM_TYPES_H__\n#define __GST_PLAY_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_PLAY_ENUM_TYPES_H__ */" \
	$^ > $@

playenumtypes.c: $(play_headers)
	glib-mkenums \
	--fhead "#include <play.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$^ > $@
