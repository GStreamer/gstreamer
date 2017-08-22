noinst_PROGRAMS = 

if USE_OMX_TARGET_RPI
noinst_PROGRAMS += testegl
else
if HAVE_X11
noinst_PROGRAMS += testegl
endif
endif

testegl_SOURCES = testegl.c

noinst_HEADERS = cube_texture_and_coords.h

testegl_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) \
  -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_GL_LIBS) \
	-lm

if HAVE_GLES2
testegl_LDADD += $(GLES2_LIBS)
endif

if HAVE_EGL
testegl_LDADD += $(EGL_LIBS)
endif

if HAVE_X11
testegl_LDADD += $(X11_LIBS)
endif

if USE_OMX_TARGET_RPI
testegl_LDADD += -lbcm_host
endif

testegl_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_GL_CFLAGS)
