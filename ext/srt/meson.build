srt_sources = [
  'gstsrt.c',
  'gstsrtbasesrc.c',
  'gstsrtclientsrc.c',
  'gstsrtserversrc.c',
  'gstsrtbasesink.c',
  'gstsrtclientsink.c',
  'gstsrtserversink.c',
]

srt_dep = dependency('srt', required : false)

if not srt_dep.found() and cc.has_header_symbol('srt/srt.h', 'srt_startup')
  srt_dep = cc.find_library('srt', required : false)
endif

if srt_dep.found()
  gstsrt = library('gstsrt',
    srt_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gio_dep, srt_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
