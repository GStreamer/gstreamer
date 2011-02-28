if USE_AALIB
AALIB_DIR = aalib
else
AALIB_DIR =
endif

if USE_ANNODEX
ANNODEX_DIR = annodex
else
ANNODEX_DIR =
endif

if USE_CAIRO
CAIRO_DIR = cairo
else
CAIRO_DIR =
endif

if USE_ESD
ESD_DIR = esd
else
ESD_DIR =
endif

if USE_FLAC
FLAC_DIR = flac
else
FLAC_DIR =
endif

if USE_GCONF
GCONF_DIR = gconf
else
GCONF_DIR =
endif

if USE_GDK_PIXBUF
GDK_PIXBUF_DIR = gdk_pixbuf
else
GDK_PIXBUF_DIR =
endif

if USE_HAL
HAL_DIR = hal
else
HAL_DIR =
endif

if USE_JACK
JACK_DIR=jack
else
JACK_DIR=
endif

if USE_JPEG
JPEG_DIR = jpeg
else
JPEG_DIR =
endif

if USE_LIBCACA
LIBCACA_DIR = libcaca
else
LIBCACA_DIR =
endif

if USE_LIBDV
LIBDV_DIR = dv
else
LIBDV_DIR =
endif

# if USE_LIBMNG
# LIBMNG_DIR = libmng
# else
LIBMNG_DIR =
# endif

if USE_LIBPNG
LIBPNG_DIR = libpng
else
LIBPNG_DIR =
endif

# if USE_MIKMOD
# MIKMOD_DIR = mikmod
# else
MIKMOD_DIR =
# endif

if USE_DV1394
DV1394_DIR = raw1394
else
DV1394_DIR =
endif

if USE_PULSE
PULSE_DIR = pulse
else
PULSE_DIR =
endif


if USE_SHOUT2
SHOUT2_DIR = shout2
else
SHOUT2_DIR =
endif

if USE_SOUP
SOUP_DIR=soup
else
SOUP_DIR=
endif

if USE_SPEEX
SPEEX_DIR = speex
else
SPEEX_DIR =
endif

if USE_TAGLIB
TAGLIB_DIR = taglib
else
TAGLIB_DIR =
endif

if USE_WAVPACK
WAVPACK_DIR=wavpack
else
WAVPACK_DIR=
endif

SUBDIRS = \
	$(AALIB_DIR) \
	$(ANNODEX_DIR) \
	$(CAIRO_DIR) \
	$(DV1394_DIR) \
	$(ESD_DIR) \
	$(FLAC_DIR) \
	$(GCONF_DIR) \
	$(GDK_PIXBUF_DIR) \
	$(HAL_DIR) \
	$(JACK_DIR) \
	$(JPEG_DIR) \
	$(LIBCACA_DIR) \
	$(LIBDV_DIR) \
	$(LIBMNG_DIR) \
	$(LIBPNG_DIR) \
	$(MIKMOD_DIR) \
	$(PULSE_DIR) \
	$(SHOUT2_DIR) \
	$(SOUP_DIR) \
	$(SPEEX_DIR) \
	$(TAGLIB_DIR) \
	$(WAVPACK_DIR)

DIST_SUBDIRS = \
	aalib \
	annodex \
	cairo \
	dv \
	esd \
	flac \
	gconf \
	gdk_pixbuf \
	hal \
	jack \
	jpeg \
	libcaca \
	libpng \
	pulse \
	raw1394 \
	shout2 \
	soup \
	speex \
	taglib \
	wavpack

include $(top_srcdir)/common/parallel-subdirs.mak
