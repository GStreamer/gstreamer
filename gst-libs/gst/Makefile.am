if HAVE_EGL
EGL_DIR = egl
endif

SUBDIRS = interfaces signalprocessor video basecamerabinsrc codecparsers \
	 insertbin $(EGL_DIR)

noinst_HEADERS = gst-i18n-plugin.h gettext.h glib-compat-private.h
DIST_SUBDIRS = interfaces egl signalprocessor video basecamerabinsrc codecparsers \
	insertbin

