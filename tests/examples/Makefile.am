if HAVE_GTK
if USE_JACK
JACK_EXAMPLES=jack
else
JACK_EXAMPLES=
endif
GTK_EXAMPLES=camerabin mxf scaletempo
else
GTK_EXAMPLES=
endif

if USE_DIRECTFB
DIRECTFB_DIR=directfb
else
DIRECTFB_DIR=
endif

SUBDIRS= $(DIRECTFB_DIR) $(GTK_EXAMPLES) $(JACK_EXAMPLES) switch
DIST_SUBDIRS= camerabin directfb jack mxf scaletempo switch
