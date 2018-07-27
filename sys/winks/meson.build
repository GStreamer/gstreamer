winks_sources = [
  'gstksclock.c',
  'gstksvideodevice.c',
  'gstksvideosrc.c',
  'ksdeviceprovider.c',
  'kshelpers.c',
  'ksvideohelpers.c',
]

if host_system == 'windows'
  winks_dep = [cc.find_library('ksuser', required : get_option('winks')),
               cc.find_library('uuid', required : get_option('winks')),
               cc.find_library('strmiids', required : get_option('winks')),
               cc.find_library('dxguid', required : get_option('winks')),
               cc.find_library('setupapi', required : get_option('winks')),
               cc.find_library('ole32', required : get_option('winks'))]

  gstwinks = library('gstwinks',
    winks_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep] + winks_dep,
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstwinks, install_dir : plugins_pkgconfig_install_dir)
endif
