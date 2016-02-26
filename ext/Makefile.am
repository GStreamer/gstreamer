if USE_ALSA
ALSA_DIR=alsa
else
ALSA_DIR=
endif

if USE_CDPARANOIA
CDPARANOIA_DIR=cdparanoia
else
CDPARANOIA_DIR=
endif

if USE_LIBVISUAL
LIBVISUAL_DIR=libvisual
else
LIBVISUAL_DIR=
endif

if USE_OGG
OGG_DIR=ogg
else
OGG_DIR=
endif

if USE_OPUS
OPUS_DIR=opus
else
OPUS_DIR=
endif

if USE_PANGO
PANGO_DIR = pango
else
PANGO_DIR =
endif

if USE_VORBIS
VORBIS_DIR=vorbis
endif

if USE_IVORBIS
VORBIS_DIR=vorbis
endif

if !USE_VORBIS
if !USE_IVORBIS
VORBIS_DIR=
endif
endif

if USE_THEORA
THEORA_DIR=theora
else
THEORA_DIR=
endif

SUBDIRS = \
  $(ALSA_DIR) \
  $(CDPARANOIA_DIR) \
  $(LIBVISUAL_DIR) \
  $(OGG_DIR) \
  $(OPUS_DIR) \
  $(PANGO_DIR) \
  $(THEORA_DIR) \
  $(VORBIS_DIR)

DIST_SUBDIRS = \
  alsa \
  cdparanoia \
  libvisual \
  ogg \
  opus \
  pango \
  theora \
  vorbis

include $(top_srcdir)/common/parallel-subdirs.mak
