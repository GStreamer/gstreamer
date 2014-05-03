
SUBDIRS =

if USE_OPENGL

if HAVE_WINDOW_COCOA
SUBDIRS += cocoa
else

SUBDIRS += generic qt

if HAVE_CLUTTER
SUBDIRS += clutter
endif

if HAVE_SDL
SUBDIRS += sdl
endif

if HAVE_GTK3
SUBDIRS += gtk
endif

endif

endif

DIST_SUBDIRS=cocoa generic qt clutter sdl gtk
