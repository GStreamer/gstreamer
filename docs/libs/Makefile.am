GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
MODULE=gst-rtsp-server
DOC_MODULE=$(MODULE)

# for upload-doc.mak
DOC=$(MODULE)
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# generated basefiles
#basefiles = \
##		$(DOC_MODULE).types \
#		$(DOC_MODULE)-sections.txt \
#		$(DOC_MODULE)-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-libs-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-libs-@GST_API_VERSION@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_API_VERSION
#$(basefiles): gstreamer-libs-@GST_API_VERSION@%: gstreamer-libs%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst/rtsp-server/
DOC_BUILD_DIR=$(top_builddir)/gst/rtsp-server/

SCAN_OPTIONS= 

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*.c

SCANOBJ_DEPS = \
	$(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_API_VERSION@.la

# Extra options to supply to gtkdoc-scan.
SCANOBJ_OPTIONS=--type-init-func="g_type_init();gst_init(&argc,&argv)"

# Header files to ignore when scanning.
IGNORE_HFILES = 
IGNORE_CFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = 

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = 

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GCOV_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
