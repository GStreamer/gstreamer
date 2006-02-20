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

# if USE_GST_V4L2
# V4L2_DIR=v4l2
# else
# V4L2_DIR=
# endif

# if USE_VCD
# VCD_DIR=vcd
# else
# VCD_DIR=
# endif

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

if USE_X
XIMAGESRC_DIR=ximagesrc
else
XIMAGESRC_DIR=
endif

SUBDIRS = $(GL_DIR) $(XIMAGESRC_DIR)

DIST_SUBDIRS = glsink ximagesrc
