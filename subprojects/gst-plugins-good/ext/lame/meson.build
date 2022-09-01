lame_dep = dependency('', required: false)
lame_option = get_option('lame')

if lame_option.disabled()
  subdir_done()
endif

lame_extra_c_args = []
lame_dep = cc.find_library('mp3lame', required: false)
have_lame = cc.has_header_symbol('lame/lame.h', 'lame_init')
if have_lame
  if cc.has_header_symbol('lame/lame.h', 'lame_set_VBR_quality')
    lame_extra_c_args += ['-DHAVE_LAME_SET_VBR_QUALITY']
  endif
  if cc.has_header_symbol('lame/lame.h', 'MEDIUM')
    lame_extra_c_args += ['-DGSTLAME_PRESET']
  endif
else
  lame_dep = dependency('mp3lame', fallback: ['lame', 'lame_dep'], required: lame_option)
  have_lame = lame_dep.found()
  lame_extra_c_args += ['-DHAVE_LAME_SET_VBR_QUALITY', '-DGSTLAME_PRESET', '-DLAME_SUBPROJECT']
endif

if not have_lame
  subdir_done()
endif

lame = library('gstlame',
  ['gstlamemp3enc.c', 'plugin.c'],
  c_args : gst_plugins_good_args + lame_extra_c_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstaudio_dep, lame_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [lame]
