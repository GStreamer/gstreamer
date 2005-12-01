## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
# DOC_MODULE=gstreamer-@GST_MAJORMINOR@
DOC_MODULE=gstreamer

BUILT_SOURCES=gstreamer.types

# for upload.mak
DOC=gstreamer
FORMATS=html
html: html-build.stamp
include $(srcdir)/../upload.mak

# generated basefiles
#basefiles = \
#		gstreamer-@GST_MAJORMINOR@.types \
#		gstreamer-@GST_MAJORMINOR@-sections.txt \
#		gstreamer-@GST_MAJORMINOR@-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-@GST_MAJORMINOR@%: gstreamer%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --ignore-files=parse

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=

# The files containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst
DOC_BUILD_DIR = $(top_builddir)/gst

HFILE_GLOB=$(top_srcdir)/gst/*.h $(top_srcdir)/libs/gst/*/*.h
CFILE_GLOB=$(top_srcdir)/gst/*.c $(top_srcdir)/libs/gst/*/*.c

SCANOBJ_DEPS = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la

# Header files to ignore when scanning. Use base file name, no paths
IGNORE_HFILES= \
	gettext.h \
	gst-i18n-lib.h \
	gst-i18n-app.h \
	gst_private.h \
	gstdata_private.h \
	gstarch.h \
	cothreads.h \
	cothreads_compat.h \
	gthread-cothreads.h \
	faircothreads.h \
	types.h \
	glib-compat.h \
	grammar.tab.h \
	gstmarshal.h \
	gstaggregator.h \
	gstbufferstore.h \
	gstfdsink.h \
	gstfdsrc.h \
	gstidentity.h \
	gstmacros.h \
	gstmd5sink.h \
	gstmultifilesrc.h \
	gstmultifdsink.h \
	gstpipefilter.h \
	gstshaper.h \
	gststatistics.h \
	gsttee.h \
	gsttrashstack.h \
	gsttypefindelement.h \
	gstspider.h \
	gstspideridentity.h \
	gstsearchfuncs.h \
	gsttrashstack.h \
	gstxmlregistry.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = running.xml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_OBJ_CFLAGS)
GTKDOC_LIBS = $(GST_OBJ_LIBS) $(SCANOBJ_DEPS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
