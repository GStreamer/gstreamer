# if USE_DXR3
# DXR3_DIR=dxr3
# else
# DXR3_DIR=
# endif

# if USE_QCAM
# QCAM_DIR=qcam
# else
# QCAM_DIR=
# endif

if USE_VCD
VCD_DIR=vcd
else
VCD_DIR=
endif

# if USE_CDROM
#  CDROM_DIR=cdrom
# else
# CDROM_DIR=
# endif

if USE_FBDEV
FBDEV_DIR=fbdev
else
FBDEV_DIR=
endif

if USE_DVB
DVB_DIR=dvb
else
DVB_DIR=
endif

if USE_OSS4
OSS4_DIR=oss4
else
OSS4_DIR=
endif

if USE_QUICKTIME
QT_DIR=qtwrapper
else
QT_DIR=
endif

if USE_WININET
WININET_DIR=wininet
else
WININET_DIR=
endif

if USE_ACM
ACM_DIR=acmenc
else
ACM_DIR=
endif

SUBDIRS = $(ACM_DIR) $(DVB_DIR) $(FBDEV_DIR) $(OSS4_DIR) $(QT_DIR) $(VCD_DIR) $(WININET_DIR)

DIST_SUBDIRS = acmenc dvb fbdev dshowdecwrapper dshowsrcwrapper dshowvideosink \
			   oss4 qtwrapper vcd wininet winscreencap

