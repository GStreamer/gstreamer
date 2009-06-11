## Process this file with automake to produce Makefile.in

# FIXME: fix the docs then remove this variable
DOCS_ARE_INCOMPLETE_PLEASE_FIXME=yespleasedo

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
DOC_MODULE=gst-plugins-base-libs

# for upload-doc.mak
DOC=gst-plugins-base-libs
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
DOC_BUILD_DIR=$(top_builddir)/gst-libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode  --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/cdda/libgstcdda-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/rtsp/libgstrtsp-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/sdp/libgstsdp-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_MAJORMINOR@.la

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
GTKDOC_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc.mak
