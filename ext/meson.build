# subdir('aalib')
subdir('cairo')
subdir('flac')
subdir('gdk_pixbuf')
subdir('jack')
subdir('jpeg')
# subdir('libcaca')
# FIXME: dv plugin fails to link with msvc, wants pthread.lib
if cc.get_id() != 'msvc'
  subdir('dv')
endif
subdir('libpng')
# subdir('raw1394')
subdir('pulse')
subdir('shout2')
subdir('soup')
subdir('speex')
# FIXME: taglib fails to compile and link with msvc
if cc.get_id() == 'msvc'
  message('Building with MSVC, disabling taglib support. Patches welcome!')
else
  subdir('taglib')
endif
subdir('vpx')
subdir('wavpack')
