dshowdecwrapper_sources = [
  'gstdshowaudiodec.cpp',
  'gstdshowdecwrapper.cpp',
  'gstdshowfakesrc.cpp',
  'gstdshowutil.cpp',
  'gstdshowvideodec.cpp'
]

if not strmbase_dep.found()
  message('strmbase not found, not building dshowdecwrapper')
  subdir_done()
endif

winmm_dep = cxx.find_library('winmm', required: get_option('directshow'))
if not winmm_dep.found()
  message('winmm not found, not building dshowdecwrapper')
  subdir_done()
endif

dmoguids_dep = cxx.find_library('dmoguids', required: get_option('directshow'))
if not dmoguids_dep.found()
  message('dmoguids not found, not building dshowdecwrapper')
  subdir_done()
endif

wmcodecdspuuid_dep = cxx.find_library('wmcodecdspuuid', required: get_option('directshow'))
if not wmcodecdspuuid_dep.found()
  message('wmcodecdspuuid not found, not building dshowdecwrapper')
  subdir_done()
endif

gstdshowdecwrapper = library('gstdshowdecwrapper',
  dshowdecwrapper_sources,
  cpp_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [
    gstaudio_dep,
    gstvideo_dep,
    strmbase_dep,
    winmm_dep,
    dmoguids_dep,
    wmcodecdspuuid_dep
  ],
  install : true,
  install_dir : plugins_install_dir,
  override_options : ['cpp_std=none'])

pkgconfig.generate(gstdshowdecwrapper, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstdshowdecwrapper]
