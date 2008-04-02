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

SUBDIRS = $(FBDEV_DIR) $(DVB_DIR) $(VCD_DIR) $(QT_DIR) $(OSS4_DIR)

DIST_SUBDIRS = dvb fbdev dshowdecwrapper dshowsrcwrapper oss4 qtwrapper vcd

