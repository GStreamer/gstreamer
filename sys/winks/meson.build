winks_sources = [
  'gstksclock.c',
  'gstksvideodevice.c',
  'gstksvideosrc.c',
  'ksdeviceprovider.c',
  'kshelpers.c',
  'ksvideohelpers.c',
]

if host_system == 'windows'
  winks_dep = [cc.find_library('ksuser'), cc.find_library('uuid'),
               cc.find_library('strmiids'), cc.find_library('dxguid'),
               cc.find_library('setupapi'), cc.find_library('ole32')]

  gstwinks = library('gstwinks',
    winks_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep] + winks_dep,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
