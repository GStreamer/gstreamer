### FIXME use HAVE_ stuff to decide on dirs
if USE_OSS
OSS_SUBDS=oss
else
OSS_SUBDS=
endif

if USE_V4L
V4L_SUBDS=v4l
else
V4L_SUBDS=
endif

if USE_VGA
VGA_SUBDS=vga
else
VGA_SUBDS=
endif

SUBDIRS=$(OSS_SUBDS) qcam $(V4L_SUBDS) vcd $(VGA_SUBDS) xvideo

DIST_SUBDIRS=oss qcam v4l vcd vga xvideo

