# if USE_A52DEC
# A52DEC_DIR = a52dec
# else
A52DEC_DIR =
# endif

if USE_AMRNB
 AMRNB_DIR = amrnb
else
 AMRNB_DIR =
endif

# if USE_DVDREAD
# DVDREAD_DIR = dvdread
# else
DVDREAD_DIR =
# endif

# if USE_DVDNAV
# DVDNAV_DIR = dvdnav
# else
DVDNAV_DIR =
# endif

if USE_LAME
LAME_DIR = lame
else
LAME_DIR =
endif

if USE_MAD
MAD_DIR = mad
else
MAD_DIR =
endif

if USE_MPEG2DEC
MPEG2DEC_DIR = mpeg2dec
else
MPEG2DEC_DIR =
endif

if USE_SIDPLAY
SIDPLAY_DIR = sidplay
else
SIDPLAY_DIR =
endif

SUBDIRS = \
	$(A52DEC_DIR) \
	$(AMRNB_DIR) \
	$(DVDNAV_DIR) \
	$(LAME_DIR) \
	$(MAD_DIR) \
	$(MPEG2DEC_DIR) \
	$(SIDPLAY_DIR)

DIST_SUBDIRS = \
	amrnb \
	lame \
	mad \
	mpeg2dec \
	sidplay
