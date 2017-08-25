lame_dep = cc.find_library('mp3lame', required : false)

if lame_dep.found() and cc.has_header_symbol('lame/lame.h', 'lame_init')
  lame_extra_c_args = []
  if cc.has_header_symbol('lame/lame.h', 'lame_set_VBR_quality')
    lame_extra_c_args += ['-DHAVE_LAME_SET_VBR_QUALITY']
  endif
  if cc.has_header_symbol('lame/lame.h', 'MEDIUM')
    lame_extra_c_args += ['-DGSTLAME_PRESET']
  endif
  lame = library('gstlame',
    ['gstlamemp3enc.c', 'plugin.c'],
    c_args : ugly_args + lame_extra_c_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, lame_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
