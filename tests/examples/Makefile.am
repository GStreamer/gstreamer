if HAVE_GTK
if USE_JACK
JACK_EXAMPLES=jack
else
JACK_EXAMPLES=
endif
GTK_EXAMPLES=camerabin mxf scaletempo camerabin2
else
GTK_EXAMPLES=
endif

if USE_DIRECTFB
DIRECTFB_DIR=directfb
else
DIRECTFB_DIR=
endif

if BUILD_EXPERIMENTAL
CAMERABIN2=camerabin2
else
CAMERABIN2=
endif

SUBDIRS= $(DIRECTFB_DIR) $(GTK_EXAMPLES) $(JACK_EXAMPLES) switch
DIST_SUBDIRS= camerabin $(CAMERABIN2) directfb jack mxf scaletempo switch
