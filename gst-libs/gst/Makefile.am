
if USE_OPENGL
GL_DIR = gl
endif
if USE_GLES2
GL_DIR = gl
endif

SUBDIRS = interfaces basecamerabinsrc codecparsers \
	 insertbin uridownloader mpegts $(GL_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = interfaces gl basecamerabinsrc codecparsers \
	insertbin uridownloader mpegts
