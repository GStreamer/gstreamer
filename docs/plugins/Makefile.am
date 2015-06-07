GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
MODULE=gst-libav
DOC_MODULE=$(MODULE)-plugins

# for upload-doc.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(srcdir)/../../common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/ext/libav

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--rebuild-sections

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
# FIXME get the location of the installed gstreamer docs
#FIXXREF_OPTIONS=--extra-dir=../gst/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/ext/libav/*.h
CFILE_GLOB=$(top_srcdir)/ext/libav/*.c

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

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(LIBAV_CFLAGS) $(GST_CFLAGS) -I$(top_builddir) -I$(top_builddir)/ext/libav
GTKDOC_LIBS = -L$(top_builddir)/ext/libav -lgstlibav \
  $(LIBAV_LIBS) $(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
