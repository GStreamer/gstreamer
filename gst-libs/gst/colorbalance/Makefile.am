libgstcolorbalanceincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

colorbalance_headers = \
	colorbalance.h \
	colorbalancechannel.h

built_headers =	\
	colorbalancemarshal.h
	colorbalanceenumtypes.h

libgstcolorbalanceinclude_HEADERS = $(colorbalance_headers) $(built_headers)

noinst_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c \
	colorbalancemarshal.c \
	colorbalanceenumtypes.c
libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)

BUILT_SOURCES = \
	colorbalancemarshal.c \
	colorbalancemarshal.h \
	colorbalanceenumtypes.c \
	colorbalanceenumtypes.h

EXTRA_DIST = colorbalancemarshal.list

CLEANFILES = $(BUILT_SOURCES)

colorbalancemarshal.h: colorbalancemarshal.list
	glib-genmarshal --header --prefix=gst_color_balance_marshal $^ > colorbalancemarshal.h.tmp
	mv colorbalancemarshal.h.tmp colorbalancemarshal.h

colorbalancemarshal.c: colorbalancemarshal.list
	echo "#include \"colorbalancemarshal.h\"" >> colorbalancemarshal.c.tmp
	glib-genmarshal --body --prefix=gst_color_balance_marshal $^ >> colorbalancemarshal.c.tmp
	mv colorbalancemarshal.c.tmp colorbalancemarshal.c

colorbalanceenumtypes.h: $(colorbalance_headers)
	glib-mkenums \
	--fhead "#ifndef __GST_COLOR_BALANCE_ENUM_TYPES_H__\n#define __GST_COLOR_BALANCE_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_COLOR_BALANCE_ENUM_TYPES_H__ */" \
	$^ > $@

colorbalanceenumtypes.c: $(colorbalance_headers)
	glib-mkenums \
	--fhead "#include <colorbalance.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$^ > $@
