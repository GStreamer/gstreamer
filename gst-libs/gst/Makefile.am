if HAVE_EGL
EGL_DIR = egl
endif

if USE_OPENGL
GL_DIR = gl
endif
if USE_GLES2
GL_DIR = gl
endif

SUBDIRS = interfaces basecamerabinsrc codecparsers \
	 insertbin uridownloader mpegts $(EGL_DIR) $(GL_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = interfaces egl gl basecamerabinsrc codecparsers \
	insertbin uridownloader mpegts
