GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
MODULE=ges
DOC_MODULE=$(MODULE)

# for upload-doc.mak
DOC=gstreamer-editing-services
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/ges

SCAN_OPTIONS= 

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/ges/ges-*.h
CFILE_GLOB=$(top_srcdir)/ges/ges-*.c

# Extra options to supply to gtkdoc-scan.
SCANOBJ_OPTIONS=--type-init-func="g_type_init();gst_init(&argc,&argv)"

# Header files to ignore when scanning.
IGNORE_HFILES = gesmarshal.h ges-internal.h
IGNORE_CFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = \
	$(top_srcdir)/ges/ges-types.h

# Images to copy into HTML directory.
HTML_IMAGES =  layer_track_overview.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = architecture.xml 

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)  $(GIO_CFLAGS) $(GCOV_CFLAGS)
GTKDOC_LIBS =  \
	$(top_builddir)/ges/libges-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS) $(GCOV_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
