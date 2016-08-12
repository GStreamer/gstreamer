subdir('alsa')
subdir('cdparanoia')
if cc.get_id() != 'msvc'
  # libvisual has MinGW-specific headers which are not compatible with MSVC
  # If we want to build this with MSVC, we must build libvisual with it too
  subdir('libvisual')
endif
subdir('ogg')
subdir('opus')
subdir('pango')
subdir('theora')
subdir('vorbis')
