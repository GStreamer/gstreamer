### FIXME use HAVE_ stuff to decide on dirs
if USE_OSS
OSS_DIR=oss
else
OSS_DIR=
endif

if USE_QCAM
QCAM_DIR=qcam
else
QCAM_DIR=
endif

if USE_V4L
V4L_DIR=v4l
else
V4L_DIR=
endif

if USE_VCD
VCD_DIR=vcd
else
VCD_DIR=
endif

if USE_VGA
VGA_DIR=vga
else
VGA_DIR=
endif

if USE_XVIDEO
XVIDEO_DIR=xvideo
else
XVIDEO_DIR=
endif

SUBDIRS=$(OSS_DIR) $(QCAM_DIR) $(V4L_DIR) $(VCD_DIR) \
	$(VGA_DIR) $(XVIDEO_DIR)

DIST_SUBDIRS=oss qcam v4l vcd vga xvideo

