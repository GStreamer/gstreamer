mpg123_sources = [
  'gstmpg123audiodec.c',
]

mpg123_dep = dependency('libmpg123', version : '>= 1.3', required : false)

if mpg123_dep.found()
  gstmpg123 = library('gstmpg123',
    mpg123_sources,
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, mpg123_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
