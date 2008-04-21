## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
# DOC_MODULE=gstreamer-@GST_MAJORMINOR@
DOC_MODULE=gstreamer

BUILT_SOURCES=gstreamer.types

# for upload.mak
DOC=gstreamer
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload.mak

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

# The files containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst
DOC_BUILD_DIR = $(top_builddir)/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml --ignore-files=parse

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*.c

SCANOBJ_DEPS = 

# Header files to ignore when scanning. Use base file name, no paths
IGNORE_HFILES= \
	gettext.h \
	glib-compat-private.h \
	glib-compat.h \
	gst-i18n-app.h \
	gst-i18n-lib.h \
	gst_private.h \
	gstmarshal.h \
	gstmacros.h \
	\
	grammar.tab.h \
	grammar.tab.pre.h \
	types.h

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
