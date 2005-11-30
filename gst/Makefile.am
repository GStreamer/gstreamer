common_cflags = $(PYGTK_CFLAGS) $(GST_CFLAGS) $(PYGST_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS)
common_ldflags = -module -avoid-version

pkgpythondir = $(pythondir)/gst-$(GST_MAJORMINOR)/gst
pkgpyexecdir = $(pyexecdir)/gst-$(GST_MAJORMINOR)/gst

pygstdir = $(pkgpythondir)
pygst_PYTHON = __init__.py

pygstexecdir = $(pkgpyexecdir)
pygstexec_LTLIBRARIES = _gst.la $(interface_lib)

interface_lib = interfaces.la

defs_DATA = gst-types.defs \
	gst-extrafuncs.defs \
	libs.defs
defsdir = $(pkgdatadir)/0.9/defs

noinst_HEADERS = common.h pygstvalue.h pygstminiobject.h pygstobject.h pygstexception.h

INCLUDES = $(PYTHON_INCLUDES)
EXTRA_DIST = $(defs_DATA) common.h arg-types.py
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@
GEN_FILES = arg-types.py gst-types.defs libs.defs

# GStreamer bindings
_gst_la_CFLAGS = $(common_cflags)
_gst_la_LIBADD = $(common_libadd)
_gst_la_LDFLAGS = $(common_ldflags) -export-symbols-regex init_gst \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_NET_LIBS)
_gst_la_SOURCES = 		\
	gst-argtypes.c 		\
	gstmodule.c 		\
	pygstiterator.c		\
	pygstminiobject.c 	\
	pygstobject.c		\
	pygstvalue.c 		\
	pygstexception.c

nodist__gst_la_SOURCES = gst.c
GST_OVERRIDES = 		\
	gst.override 		\
	gstbin.override 	\
	gstbuffer.override 	\
	gstbus.override		\
	gstevent.override	\
	gstcaps.override 	\
	gstelement.override 	\
	gstelementfactory.override \
	gstmessage.override	\
	gstobject.override	\
	gstquery.override	\
	gstpad.override		\
	gststructure.override	\
	gsttaglist.override	\
	gstlibs.override

GST_DEFS = gst.defs gst-types.defs gst-extrafuncs.defs libs.defs
CLEANFILES = gst.c
EXTRA_DIST += $(GST_DEFS) $(GST_OVERRIDES)
gst.c: $(GST_DEFS) $(GST_OVERRIDES) $(GEN_FILES)

# GStreamer interfaces bindings
interfaces_la_CFLAGS = $(common_cflags) $(GST_BASE_CFLAGS)
interfaces_la_LIBADD = $(common_libadd) -lgstinterfaces-0.9 
interfaces_la_LDFLAGS = $(common_ldflags) -export-symbols-regex initinterface $(GST_BASE_LIBS)
interfaces_la_SOURCES = interfacesmodule.c
nodist_interfaces_la_SOURCES = interfaces.c
INTERFACES_OVERRIDES = interfaces.override xoverlay.override
INTERFACES_DEFS = interfaces.defs xoverlay.defs xwindowlistener.defs
CLEANFILES += interfaces.c
EXTRA_DIST += $(INTERFACES_DEFS) $(INTERFACES_OVERRIDES) 
interfaces.c: $(INTERFACES_DEFS) $(INTERFACES_OVERRIDES) $(GEN_FILES)

.defs.c: 
	(cd $(srcdir) \
	&& python $(top_srcdir)/codegen/codegen.py \
		--load-types $(srcdir)/arg-types.py \
		--register $(srcdir)/gst-types.defs \
		--override $*.override \
		--prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

SUBDIRS = extend
