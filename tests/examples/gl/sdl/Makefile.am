noinst_PROGRAMS = ##

#works on win32 and X
if HAVE_SDL

noinst_PROGRAMS += sdlshare

sdlshare_SOURCES = sdlshare.c

sdlshare_CFLAGS=$(GST_PLUGINS_GL_CFLAGS) $(GST_CFLAGS) \
    $(GL_CFLAGS) $(SDL_CFLAGS)
sdlshare_LDADD=$(GST_PLUGINS_GL_LIBS) $(GST_LIBS) \
    $(GL_LIBS) $(SDL_LIBS)

endif
