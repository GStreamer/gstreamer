common_cflags = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS) $(GST_OPTION_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS) $(GST_OPTION_LIBS)
common_ldflags = -module -avoid-version

pkgpyexecdir = $(pyexecdir)/gst-$(GST_MAJORMINOR)/gst

# we install everything in pyexecdir; otherwise you end up with a mess for
# multilib
pygstdir = $(pkgpyexecdir)
pygst_PYTHON = __init__.py

pygstexecdir = $(pkgpyexecdir)
pygstexec_LTLIBRARIES = _gst.la $(interface_lib) $(pbutils_lib)

interface_lib = interfaces.la
pbutils_lib = pbutils.la

defs_DATA = gst-types.defs \
	gst-extrafuncs.defs \
	libs.defs \
	base.defs \
	pbutils.defs
defsdir = $(pkgdatadir)/$(GST_MAJORMINOR)/defs

pygst_installdir = $(includedir)/gstreamer-@GST_MAJORMINOR@/pygst/
pygst_install_HEADERS = pygst.h common.h pygstvalue.h pygstminiobject.h pygstexception.h

versioned_overrides = \
	gst-0.10.21.ignore 	\
	gst-0.10.22.ignore 	\
	gst-0.10.23.ignore 	\
	gst-0.10.24.ignore 	\
	gst-0.10.25.ignore 	\
	gst-0.10.26.ignore 	\
	gst-0.10.29.ignore 	\
	gst-0.10.30.ignore 	\
	gst-0.10.31.ignore 	\
	gst-0.10.32.ignore 	\
	gst-pb-0.10.23.ignore	\
	gst-pb-0.10.25.ignore	\
	gst-pb-0.10.26.ignore	\
	gst-pb-0.10.29.ignore	\
	gst-pb-0.10.30.ignore	\
	gst-pb-0.10.31.ignore	\
	gst-pb-0.10.32.ignore	\
	gst-disable-loadsave.ignore

INCLUDES = $(PYTHON_INCLUDES)
EXTRA_DIST = $(defs_DATA) $(versioned_overrides) common.h arg-types.py
GEN_FILES = arg-types.py gst-types.defs libs.defs base.defs pbutils.defs

# GStreamer bindings
_gst_la_CFLAGS = $(common_cflags)
_gst_la_LIBADD = $(common_libadd) $(GST_BASE_LIBS)
_gst_la_LDFLAGS = $(common_ldflags) -export-symbols-regex "^(init_gst|_PyGObject_API|pygstminiobject_).*" \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_NET_LIBS) $(GST_DP_LIBS)
_gst_la_SOURCES = 		\
	gst-argtypes.c 		\
	gstmodule.c 		\
	pygstiterator.c		\
	pygstminiobject.c 	\
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
	gstlibs.override	\
	gstbase.override

GST_DEFS = gst.defs gst-types.defs gst-extrafuncs.defs libs.defs base.defs
CLEANFILES = gst.c __init__.pyc
EXTRA_DIST += $(GST_DEFS) $(GST_OVERRIDES) gstversion.override.in
gst.c: $(GST_DEFS) $(GST_OVERRIDES) $(GEN_FILES) gstversion.override __init__.py

# GStreamer interfaces bindings
interfaces_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
interfaces_la_LIBADD = $(common_libadd) -lgstinterfaces-$(GST_MAJORMINOR) 
interfaces_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(initinterface|_PyGObject_API).*" $(GST_PLUGINS_BASE_LIBS)
interfaces_la_SOURCES = interfacesmodule.c
nodist_interfaces_la_SOURCES = interfaces.c
INTERFACES_OVERRIDES = interfaces.override xoverlay.override
INTERFACES_DEFS = interfaces.defs xoverlay.defs
CLEANFILES += interfaces.c
EXTRA_DIST += $(INTERFACES_DEFS) $(INTERFACES_OVERRIDES) 
interfaces.c: $(INTERFACES_DEFS) $(INTERFACES_OVERRIDES) $(GEN_FILES)

# GStreamer pbutils bindings
pbutils_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
pbutils_la_LIBADD = $(common_libadd) $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-0.10
pbutils_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(initpbutils|_PyGObject_API).*" $(GST_PLUGINS_BASE_LIBS)
pbutils_la_SOURCES = pbutilsmodule.c gst-argtypes.c
nodist_pbutils_la_SOURCES = pbutils.c
PBUTILS_OVERRIDES = pbutils.override
PBUTILS_DEFS = pbutils.defs
CLEANFILES += pbutils.c
EXTRA_DIST += $(PBUTILS_DEFS) $(PBUTILS_OVERRIDES)
pbutils.c: $(PBUTILS_DEFS) $(PBUTILS_OVERRIDES) $(GEN_FILES)

# GStreamer audio bindings
AUDIO_OVERRIDES = audio.override
AUDIO_DEFS = audio.defs
CLEANFILES += audio.c
EXTRA_DIST += $(AUDIO_DEFS) $(AUDIO_OVERRIDES)
defs_DATA += $(AUDIO_DEFS)
GEN_FILES += $(AUDIO_DEFS)

if HAVE_GST_AUDIO

 audio_lib = audio.la
 pygstexec_LTLIBRARIES += $(audio_lib)
 audio_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
 audio_la_LIBADD = $(common_libadd) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-0.10
 audio_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(initaudio|_PyGObject_API).*" $(GST_PLUGINS_BASE_LIBS)
 audio_la_SOURCES = audiomodule.c gst-argtypes.c
 nodist_audio_la_SOURCES = audio.c
audio.c: $(AUDIO_DEFS) $(AUDIO_OVERRIDES) $(GEN_FILES)

endif

# GStreamer video bindings
VIDEO_OVERRIDES = video.override
VIDEO_DEFS = video.defs
CLEANFILES += video.c
EXTRA_DIST += $(VIDEO_DEFS) $(VIDEO_OVERRIDES)
defs_DATA += $(VIDEO_DEFS)
GEN_FILES += $(VIDEO_DEFS)

if HAVE_GST_VIDEO

 video_lib = video.la
 pygstexec_LTLIBRARIES += $(video_lib)
 video_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
 video_la_LIBADD = $(common_libadd) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-0.10
 video_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(initvideo|_PyGObject_API).*" $(GST_PLUGINS_BASE_LIBS)
 video_la_SOURCES = videomodule.c gst-argtypes.c
 nodist_video_la_SOURCES = video.c
video.c: $(VIDEO_DEFS) $(VIDEO_OVERRIDES) $(GEN_FILES)

endif

# GStreamer tag bindings
TAG_OVERRIDES = tag.override
TAG_DEFS = tag.defs
CLEANFILES += tag.c
EXTRA_DIST += $(TAG_DEFS) $(TAG_OVERRIDES)
defs_DATA += $(TAG_DEFS)
GEN_FILES += $(TAG_DEFS)

if HAVE_GST_TAG

 tag_lib = tag.la
 pygstexec_LTLIBRARIES += $(tag_lib)
 tag_la_CFLAGS = $(common_cflags) $(GST_PLUGINS_BASE_CFLAGS)
 tag_la_LIBADD = $(common_libadd) $(GST_PLUGINS_BASE_LIBS) -lgsttag-0.10
 tag_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(inittag|_PyGObject_API).*" $(GST_PLUGINS_BASE_LIBS)
 tag_la_SOURCES = tagmodule.c gst-argtypes.c
 nodist_tag_la_SOURCES = tag.c
tag.c: $(TAG_DEFS) $(TAG_OVERRIDES) $(GEN_FILES)

endif


.defs.c: 
	$(AM_V_GEN)($(PYTHON) $(top_srcdir)/codegen/codegen.py \
		--load-types $(srcdir)/arg-types.py \
		--register $(srcdir)/gst-types.defs \
		--override $(srcdir)/$*.override \
		--extendpath $(top_builddir)/gst/ \
		--extendpath $(srcdir)/ \
		--prefix py$* $<) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

SUBDIRS = extend
