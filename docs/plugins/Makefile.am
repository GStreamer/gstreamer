GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
MODULE=gstreamer
DOC_MODULE=$(MODULE)-plugins

# for upload-doc.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# Extra environment needed for Core only...
INSPECT_EXTRA_ENVIRONMENT= \
    GST_PLUGIN_SCANNER=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/plugins/elements

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(top_builddir)/docs/gst/html \
	--extra-dir=$(top_builddir)/docs/libs/html \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/plugins/elements/*.h
CFILE_GLOB=$(top_srcdir)/plugins/elements/*.c

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

EXTRA_HFILES = \
	$(top_srcdir)/plugins/elements/gstcapsfilter.h \
	$(top_srcdir)/plugins/elements/gstfakesrc.h \
	$(top_srcdir)/plugins/elements/gstfakesink.h \
	$(top_srcdir)/plugins/elements/gstfdsink.h \
	$(top_srcdir)/plugins/elements/gstfdsrc.h \
	$(top_srcdir)/plugins/elements/gstfilesrc.h \
	$(top_srcdir)/plugins/elements/gstfilesink.h \
	$(top_srcdir)/plugins/elements/gstidentity.h \
	$(top_srcdir)/plugins/elements/gstinputselector.h \
	$(top_srcdir)/plugins/elements/gstmultiqueue.h \
	$(top_srcdir)/plugins/elements/gstoutputselector.h \
	$(top_srcdir)/plugins/elements/gstqueue.h \
	$(top_srcdir)/plugins/elements/gstqueue2.h \
	$(top_srcdir)/plugins/elements/gsttypefindelement.h \
	$(top_srcdir)/plugins/elements/gsttee.h \
	$(top_srcdir)/plugins/elements/gstvalve.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_OBJ_CFLAGS) -I$(top_builddir) -I$(top_builddir)/libs
GTKDOC_LIBS = $(GST_OBJ_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
