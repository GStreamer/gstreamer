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

if USE_OPENGL
GL_DIR=glsink
else
GL_DIR=
endif

if USE_DVB
DVB_DIR=dvb
else
DVB_DIR=
endif

if USE_QUICKTIME
QT_DIR=qtwrapper
else
QT_DIR=
endif

SUBDIRS = $(GL_DIR) $(DVB_DIR) $(VCD_DIR) $(QT_DIR)

DIST_SUBDIRS = glsink dvb vcd qtwrapper
