common_cflags = $(PYGTK_CFLAGS) $(GST_CFLAGS) $(PYGST_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS)
common_ldflags = -module -avoid-version

pkgpyexecdir = $(pyexecdir)/gst-$(GST_MAJORMINOR)/gst

# we install everything in pyexecdir; otherwise you end up with a mess for
# multilib
pygstdir = $(pkgpyexecdir)
pygst_PYTHON = __init__.py

pygstexecdir = $(pkgpyexecdir)
pygstexec_LTLIBRARIES = _gst.la $(interface_lib)

interface_lib = interfaces.la

defs_DATA = gst-types.defs \
	gst-extrafuncs.defs \
	libs.defs \
	base.defs
defsdir = $(pkgdatadir)/$(GST_MAJORMINOR)/defs

noinst_HEADERS = common.h pygstvalue.h pygstminiobject.h pygstobject.h pygstexception.h

versioned_overrides = \
	gst-0.10.3.ignore	\
	gst-0.10.4.ignore	\
	gst-0.10.5.ignore

INCLUDES = $(PYTHON_INCLUDES)
EXTRA_DIST = $(defs_DATA) $(versioned_overrides) common.h arg-types.py
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@
GEN_FILES = arg-types.py gst-types.defs libs.defs base.defs

# GStreamer bindings
_gst_la_CFLAGS = $(common_cflags)
_gst_la_LIBADD = $(common_libadd) $(GST_BASE_LIBS)
_gst_la_LDFLAGS = $(common_ldflags) -export-symbols-regex init_gst \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_NET_LIBS) $(GST_DP_LIBS)
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
	gstversion.override	\
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
	gstlibs.override	\
	gstbase.override

GST_DEFS = gst.defs gst-types.defs gst-extrafuncs.defs libs.defs base.defs
CLEANFILES = gst.c
EXTRA_DIST += $(GST_DEFS) $(GST_OVERRIDES) gstversion.override.in
gst.c: $(GST_DEFS) $(GST_OVERRIDES) $(GEN_FILES) gstversion.override

# GStreamer interfaces bindings
interfaces_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
interfaces_la_LIBADD = $(common_libadd) -lgstinterfaces-$(GST_MAJORMINOR) 
interfaces_la_LDFLAGS = $(common_ldflags) -export-symbols-regex initinterface $(GST_PLUGINS_BASE_LIBS)
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
		--extendpath $(top_builddir)/gst/ \
		--prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

SUBDIRS = extend
