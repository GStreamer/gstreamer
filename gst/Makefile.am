INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

pygstreamerdir = $(pythondir)/gstreamer
pygstreamer_PYTHON = __init__.py

pygstreamerexecdir = $(pyexecdir)/gstreamer

# gstreamer binding

pyexec_LTLIBRARIES = _gstreamermodule.la
_gstreamermodule_la_SOURCES = gstreamermodule.c gstreamer-fixes.c gstreamer-fixes.h
_gstreamermodule_la_CFLAGS = $(GST_CFLAGS)
_gstreamermodule_la_LIBADD = $(GST_LIBS)
_gstreamermodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex initgstreamer
nodist__gstreamermodule_la_SOURCES = gstreamer.c
CLEANFILES = gstreamer.c
EXTRA_DIST = gstreamer.defs gstreamer.override  
gstreamer.c: gstreamer.defs gstreamer.override

.defs.c:
	(cd $(srcdir) \
	 && $(PYGTK_CODEGEN) \
	    --load-types $(top_srcdir)/gstreamer/gstreamer-arg-types.py \
	    --override $*.override \
	    --prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

#	    --register $(PYGTK_DEFSDIR)/pango-types.defs \
#	    --register $(PYGTK_DEFSDIR)/gdk-types.defs \
#	    --register $(PYGTK_DEFSDIR)/gtk-types.defs \
#	    --register $(top_srcdir)/bonobo/bonobo-types.defs \
#	    --register $(top_srcdir)/bonobo/bonoboui-types.defs \

# this should go in core

enums:
	glib-mkenums \
	--fhead "#ifndef __GST_ENUM_TYPES_H__\n#define __GST_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_ENUM_TYPES_H__ */" \
	/usr/include/gst/*.h > tmp-enum-types.h

	glib-mkenums \
	--fhead "#include <gst/gst.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	/usr/include/gst/*.h > tmp-enum-types.c
