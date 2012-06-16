GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
MODULE=gst-plugins-ugly
DOC_MODULE=$(MODULE)-plugins

# for upload-doc.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst $(top_srcdir)/ext

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB= \
  $(top_srcdir)/gst/*/*.h $(top_srcdir)/ext/*/*.h
CFILE_GLOB= \
  $(top_srcdir)/gst/*/*.c $(top_srcdir)/ext/*/*.c $ $(top_srcdir)/ext/*/*.cc

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

# FIXME: not ported yet
#	$(top_srcdir)/ext/gnomevfs/gstgnomevfssink.c 

EXTRA_HFILES = \
	$(top_srcdir)/ext/a52dec/gsta52dec.h \
	$(top_srcdir)/ext/amrnb/amrnbdec.h \
	$(top_srcdir)/ext/amrnb/amrnbenc.h \
	$(top_srcdir)/ext/amrwbdec/amrwbdec.h \
	$(top_srcdir)/ext/cdio/gstcdiocddasrc.h \
	$(top_srcdir)/ext/lame/gstlamemp3enc.h \
	$(top_srcdir)/ext/mad/gstmad.h \
	$(top_srcdir)/ext/sidplay/gstsiddec.h \
	$(top_srcdir)/ext/twolame/gsttwolamemp2enc.h \
	$(top_srcdir)/ext/x264/gstx264enc.h \
	$(top_srcdir)/gst/asfdemux/gstrtspwms.h \
	$(top_srcdir)/gst/xingmux/gstxingmux.h \
	$(top_srcdir)/gst/realmedia/rademux.h \
	$(top_srcdir)/gst/realmedia/rdtmanager.h \
	$(top_srcdir)/gst/realmedia/rmdemux.h \
	$(top_srcdir)/gst/realmedia/rtspreal.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
