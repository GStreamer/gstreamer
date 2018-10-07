
SUBDIRS =

if USE_OPENGL

if HAVE_WINDOW_COCOA
SUBDIRS += cocoa
else

SUBDIRS += generic qt

if HAVE_SDL
SUBDIRS += sdl
endif

if HAVE_GTK
SUBDIRS += gtk
endif

endif

endif

DIST_SUBDIRS=cocoa generic qt sdl gtk

include $(top_srcdir)/common/parallel-subdirs.mak
