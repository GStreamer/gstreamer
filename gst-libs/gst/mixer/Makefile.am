libgstmixerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

mixer_headers = \
	mixer.h \
	mixertrack.h

built_headers =	\
	mixermarshal.h \
	mixerenumtypes.h

libgstmixerinclude_HEADERS = $(mixer_headers) $(built_headers)

noinst_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixertrack.c \
	mixermarshal.c \
	mixerenumtypes.c
libgstmixer_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	mixermarshal.c \
	mixermarshal.h \
	mixerenumtypes.c \
	mixerenumtypes.h

EXTRA_DIST = mixermarshal.list

CLEANFILES = $(BUILT_SOURCES)

mixermarshal.h: mixermarshal.list
	glib-genmarshal --header --prefix=gst_mixer_marshal $^ > mixermarshal.h.tmp
	mv mixermarshal.h.tmp mixermarshal.h

mixermarshal.c: mixermarshal.list
	echo "#include \"mixermarshal.h\"" >> mixermarshal.c.tmp
	glib-genmarshal --body --prefix=gst_mixer_marshal $^ >> mixermarshal.c.tmp
	mv mixermarshal.c.tmp mixermarshal.c

mixerenumtypes.h: $(mixer_headers)
	glib-mkenums \
	--fhead "#ifndef __GST_MIXER_ENUM_TYPES_H__\n#define __GST_MIXER_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_MIXER_ENUM_TYPES_H__ */" \
	$^ > $@

mixerenumtypes.c: $(mixer_headers)
	glib-mkenums \
	--fhead "#include <mixer.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$^ > $@
