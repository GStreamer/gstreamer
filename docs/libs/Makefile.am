## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gstreamer-libs-@GST_MAJORMINOR@
DOC_MODULE=gstreamer-libs

# don't want $(DOC_MODULE)-scan.c to be built with -Werror
ERROR_CFLAGS=

# for upload-doc.mak
DOC=gstreamer-libs
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# generated basefiles
#basefiles = \
#		$(DOC_MODULE).types \
#		$(DOC_MODULE)-sections.txt \
#		$(DOC_MODULE)-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-libs-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-libs-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-libs-@GST_MAJORMINOR@%: gstreamer-libs%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/libs/gst
DOC_BUILD_DIR=$(top_builddir)/libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.c

# Dependencies for the intermediate scanobj tool
SCANOBJ_DEPS = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la

# Extra options to pass to gtkdoc-scanobj or gtkdoc-scangobj.
SCANOBJ_OPTIONS=--type-init-func="g_type_init();gst_init(&argc,&argv)"

# Header files to ignore when scanning.
IGNORE_HFILES = \
	gstinterpolationcontrolsourceprivate.h \
	gstlfocontrolsourceprivate.h \
	gstcontrollerprivate.h \
	gstgetbits_inl.h \
	dp-private.h

# Images to copy into HTML directory.
HTML_IMAGES = gdp-header.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I$(top_builddir) -I$(top_builddir)/libs $(GST_OBJ_CFLAGS)
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_OBJ_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

GTKDOC_EXTRA_ENVIRONMENT= \
    GST_PLUGIN_SCANNER=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
