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
APPLE_MEDIA_DIR=applemedia
else
APPLE_MEDIA_DIR=
endif

if USE_BLUEZ
BLUEZ_DIR=bluez
else
BLUEZ_DIR=
endif

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

if USE_IPCPIPELINE
IPCPIPELINE_DIR=ipcpipeline
else
IPCPIPELINE_DIR=
endif

if USE_KMS
KMS_DIR=kms
else
KMS_DIR=
endif

if USE_DVB
DVB_DIR=dvb
else
DVB_DIR=
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

if USE_NVDEC
NVDEC_DIR=nvdec
else
NVDEC_DIR=
endif

if USE_NVENC
NVENC_DIR=nvenc
else
NVENC_DIR=
endif

if USE_TINYALSA
TINYALSA_DIR=tinyalsa
else
TINYALSA_DIR=
endif

if USE_MSDK
MSDK_DIR=msdk
else
MSDK_DIR=
endif

SUBDIRS = $(ACM_DIR) $(ANDROID_MEDIA_DIR) $(APPLE_MEDIA_DIR) $(BLUEZ_DIR) $(D3DVIDEOSINK_DIR) $(DECKLINK_DIR) $(DIRECTSOUND_DIR) $(WINKS_DIR) $(DVB_DIR) $(FBDEV_DIR) $(IPCPIPELINE_DIR) $(KMS_DIR) $(OPENSLES_DIR) $(SHM_DIR) $(UVCH264_DIR) $(VCD_DIR) $(VDPAU_DIR) $(WININET_DIR) $(WINSCREENCAP_DIR) $(WASAPI_DIR) $(NVDEC_DIR) $(NVENC_DIR) $(TINYALSA_DIR) $(MSDK_DIR)

DIST_SUBDIRS = acmenc acmmp3dec androidmedia applemedia bluez d3dvideosink decklink directsound dvb fbdev ipcpipeline kms dshowdecwrapper dshowsrcwrapper dshowvideosink \
		opensles shm uvch264 vcd vdpau wasapi winks winscreencap \
		nvdec nvenc tinyalsa msdk

include $(top_srcdir)/common/parallel-subdirs.mak
