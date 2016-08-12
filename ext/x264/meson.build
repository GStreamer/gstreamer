x264_sources = [
  'gstx264enc.c',
]

x264_dep = dependency('x264', required : false)

if x264_dep.found()
  gstx264 = library('gstx264',
    x264_sources,
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, x264_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
