if USE_A52
A52_DIR=a52
else
A52_DIR=
endif

if USE_AALIB
AALIB_DIR=aalib
else
AALIB_DIR=
endif

if USE_AUDIOFILE
AUDIOFILE_DIR=audiofile
else
AUDIOFILE_DIR=
endif

if USE_ESD
ESD_DIR=esd
else
ESD_DIR=
endif

if USE_LAME
LAME_DIR=lame
else
LAME_DIR=
endif

if USE_MAD
MAD_DIR=mad
else
MAD_DIR=
endif

if USE_SDL
SDL_DIR=sdl
else
SDL_DIR=
endif

if USE_VORBIS
VORBIS_DIR=vorbis
else
VORBIS_DIR=
endif


SUBDIRS=$(A52_DIR) $(AALIB_DIR) $(AUDIOFILE_DIR) $(ESD_DIR) \
        $(LAME_DIR) $(MAD_DIR) \
	$(SDL_DIR) $(VORBIS_DIR)

DIST_SUBDIRS=a52 aalib audiofile esd lame mad sdl vorbis
