if USE_ACM
ACM_DIR=acmenc acmmp3dec
else
ACM_DIR=
endif

if USE_ANDROID_MEDIA
ANDROID_MEDIA_DIR=androidmedia
else
ANDROID_MEDIA_DIR=
endif

if USE_APPLE_MEDIA
APPLE_MEDIA_DIR=applemedia applemedia-nonpublic
else
APPLE_MEDIA_DIR=
endif

if USE_AVC
AVC_DIR=avc
else
AVC_DIR=
endif

if USE_BLUEZ
BLUEZ_DIR=bluez
else
BLUEZ_DIR=
endif

# if USE_CDROM
#  CDROM_DIR=cdrom
# else
# CDROM_DIR=
# endif

if USE_WASAPI
WASAPI_DIR=wasapi
else
WASAPI_DIR=
endif

if USE_DIRECT3D
D3DVIDEOSINK_DIR=d3dvideosink
else
D3DVIDEOSINK_DIR=
endif

if USE_DECKLINK
DECKLINK_DIR=decklink
else
DECKLINK_DIR=
endif

if USE_DIRECTDRAW
DIRECTDRAW_DIR=directdraw
else
DIRECTDRAW_DIR=
endif

if USE_WINKS
WINKS_DIR=winks
else
WINKS_DIR=
endif

if USE_DIRECTSOUND
DIRECTSOUND_DIR=directsound
else
DIRECTSOUND_DIR=
endif

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

# if USE_DXR3
# DXR3_DIR=dxr3
# else
# DXR3_DIR=
# endif

if USE_LINSYS
LINSYS_DIR=linsys
else
LINSYS_DIR=
endif

if USE_OSX_VIDEO
OSX_VIDEO_DIR=osxvideo
else
OSX_VIDEO_DIR=
endif

if USE_PVR
PVR_DIR=pvr2d
else
PVR_DIR=
endif

if USE_SHM
SHM_DIR=shm
else
SHM_DIR=
endif

if USE_OPENSLES
OPENSLES_DIR=opensles
else
OPENSLES_DIR=
endif

# if USE_QCAM
# QCAM_DIR=qcam
# else
# QCAM_DIR=
# endif

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

if USE_VCD
VCD_DIR=vcd
else
VCD_DIR=
endif

if USE_VDPAU
VDPAU_DIR=vdpau
else
VDPAU_DIR=
endif

if USE_WINSCREENCAP
WINSCREENCAP_DIR=winscreencap
else
WINSCREENCAP_DIR=
endif

if USE_UVCH264
UVCH264_DIR=uvch264
else
UVCH264_DIR=
endif

SUBDIRS = $(ACM_DIR) $(ANDROID_MEDIA_DIR) $(APPLE_MEDIA_DIR) $(AVC_DIR) $(BLUEZ_DIR) $(D3DVIDEOSINK_DIR) $(DECKLINK_DIR) $(DIRECTDRAW_DIR) $(DIRECTSOUND_DIR) $(WINKS_DIR) $(DVB_DIR) $(FBDEV_DIR) $(LINSYS_DIR) $(OPENSLES_DIR) $(OSX_VIDEO_DIR) $(PVR_DIR) $(QT_DIR) $(SHM_DIR) $(UVCH264_DIR) $(VCD_DIR) $(VDPAU_DIR) $(WININET_DIR) $(WINSCREENCAP_DIR) $(WASAPI_DIR)

DIST_SUBDIRS = acmenc acmmp3dec androidmedia applemedia applemedia-nonpublic avc bluez d3dvideosink decklink directdraw directsound dvb linsys fbdev dshowdecwrapper dshowsrcwrapper dshowvideosink \
		opensles osxvideo pvr2d qtwrapper shm uvch264 vcd vdpau wasapi wininet winks winscreencap

include $(top_srcdir)/common/parallel-subdirs.mak
