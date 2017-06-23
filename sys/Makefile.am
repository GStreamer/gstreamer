if USE_OSS
OSS_DIR=oss
else
OSS_DIR=
endif

if USE_OSS4
OSS4_DIR=oss4
else
OSS4_DIR=
endif

if USE_DIRECTSOUND
DIRECTSOUND_DIR=directsound
else
DIRECTSOUND_DIR=
endif

if USE_WAVEFORM
WAVEFORM_DIR=waveform
else
WAVEFORM_DIR=
endif

if USE_OSX_AUDIO
OSX_AUDIO_DIR=osxaudio
else
OSX_AUDIO_DIR=
endif

if USE_OSX_VIDEO
OSX_VIDEO_DIR=osxvideo
else
OSX_VIDEO_DIR=
endif

if USE_GST_V4L2
V4L2_DIR=v4l2
else
V4L2_DIR=
endif

if USE_X
XIMAGE_DIR=ximage
else
XIMAGE_DIR=
endif

SUBDIRS=$(DIRECTSOUND_DIR) $(WAVEFORM_DIR) $(OSS_DIR) $(OSS4_DIR) $(OSX_AUDIO_DIR) $(OSX_VIDEO_DIR) $(V4L2_DIR) $(XIMAGE_DIR)

DIST_SUBDIRS=directsound oss oss4 osxaudio osxvideo v4l2 waveform ximage

include $(top_srcdir)/common/parallel-subdirs.mak
