noinst_PROGRAMS = ##

#works on win32 and X
if HAVE_SDL

noinst_PROGRAMS += sdlshare sdlshare2

sdlshare_SOURCES = sdlshare.c

sdlshare_CFLAGS= \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	$(SDL_CFLAGS)
sdlshare_LDADD= $(GST_LIBS) $(GL_LIBS) $(SDL_LIBS) \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	-lgstvideo-$(GST_API_VERSION)

sdlshare2_SOURCES = sdlshare2.c

sdlshare2_CFLAGS= \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	$(SDL_CFLAGS)
sdlshare2_LDADD= $(GST_LIBS) $(GL_LIBS) $(SDL_LIBS) \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	-lgstvideo-$(GST_API_VERSION)

endif
