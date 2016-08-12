twolame_dep = dependency('twolame', version : '>= 0.3.10', required : false)

if twolame_dep.found()
  twolame = library('gsttwolame',
    ['gsttwolamemp2enc.c'],
    c_args : ugly_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, twolame_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
