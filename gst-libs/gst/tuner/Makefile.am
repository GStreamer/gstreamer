libgsttunerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tuner

tuner_headers = \
	tuner.h \
	tunernorm.h \
	tunerchannel.h

built_headers =	\
	tunermarshal.h
	tunerenumtypes.h

libgsttunerinclude_HEADERS = $(tuner_headers) $(built_headers)

noinst_LTLIBRARIES = libgsttuner.la

libgsttuner_la_SOURCES = \
	tuner.c \
	tunernorm.c \
	tunerchannel.c \
	tunermarshal.c
libgsttuner_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	tunermarshal.c \
	tunermarshal.h \
	tunerenumtypes.c \
	tunerenumtypes.h

EXTRA_DIST = tunermarshal.list

CLEANFILES = $(BUILT_SOURCES)

tunermarshal.h: tunermarshal.list
	glib-genmarshal --header --prefix=gst_tuner_marshal $^ > tunermarshal.h.tmp
	mv tunermarshal.h.tmp tunermarshal.h

tunermarshal.c: tunermarshal.list
	echo "#include \"tunermarshal.h\"" >> tunermarshal.c.tmp
	glib-genmarshal --body --prefix=gst_tuner_marshal $^ >> tunermarshal.c.tmp
	mv tunermarshal.c.tmp tunermarshal.c

tunerenumtypes.h: $(tuner_headers)
	glib-mkenums \
	--fhead "#ifndef __GST_TUNER_ENUM_TYPES_H__\n#define __GST_TUNER_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_TUNER_ENUM_TYPES_H__ */" \
	$^ > $@

tunerenumtypes.c: $(tuner_headers)
	glib-mkenums \
	--fhead "#include <tuner.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$^ > $@
