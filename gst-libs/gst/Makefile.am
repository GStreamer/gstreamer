if USE_GCONF
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if USE_XFREE
X_DIR=xwindowlistener
else
X_DIR=
endif

SUBDIRS = audio colorbalance floatcast \
		$(GCONF_DIR) idct media-info \
		mixer navigation play \
		resample riff tuner video \
		xoverlay $(X_DIR)

DIST_SUBDIRS = audio colorbalance floatcast \
		gconf idct media-info \
		mixer navigation play \
		resample riff tuner video \
		xoverlay xwindowlistener
