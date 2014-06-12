noinst_PROGRAMS = ##

#works on win32 and X
if HAVE_SDL

noinst_PROGRAMS += sdlshare

sdlshare_SOURCES = sdlshare.c

sdlshare_CFLAGS=$(GST_VIDEO_CFLAGS) $(GST_CFLAGS) \
    $(GL_CFLAGS) $(SDL_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs
sdlshare_LDADD=$(GST_VIDEO_LIBS) $(GST_LIBS) \
    $(GL_LIBS) $(SDL_LIBS) \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	-lgstvideo-$(GST_API_VERSION)

endif
