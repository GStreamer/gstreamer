## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
DOC_MODULE=gst-plugins-base-libs

# for upload.mak
DOC=gst-plugins-base-libs
FORMATS=html
html: html-build.stamp
include $(srcdir)/../upload.mak

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
DOC_SOURCE_DIR=$(top_srcdir)/gst-libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --ignore-files=trio

# Extra options to supply to gtkdoc-fixref.
# FIXME get the location of the installed gstreamer docs
#FIXXREF_OPTIONS=--extra-dir=../gst/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

#SCANOBJ_DEPS = $(top_builddir)/gst/elements/libgstelements.la \
#	$(top_builddir)/gst/schedulers/libgstbasicomegascheduler.la
#SCANOBJ_DEPS = $(top_builddir)/libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la
SCANOBJ_DEPS = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la

# Header files to ignore when scanning.
IGNORE_HFILES = 

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = compiling.sgml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir) -I$(top_builddir)/gst-libs
GTKDOC_LIBS = $(GST_BASE_LIBS) $(SCANOBJ_DEPS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
