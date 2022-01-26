dshowsinkwrapper_sources = [
  'dshowvideofakesrc.cpp',
  'dshowvideosink.cpp'
]

if not strmbase_dep.found()
  message('strmbase not found, not building dshowsinkwrapper')
  subdir_done()
endif

winmm_dep = cxx.find_library('winmm', required: get_option('directshow'))
if not winmm_dep.found()
  message('winmm not found, not building dshowsinkwrapper')
  subdir_done()
endif

mfuuid_dep = cxx.find_library('mfuuid', required: get_option('directshow'))
if not mfuuid_dep.found()
  message('mfuuid not found, not building dshowsinkwrapper')
  subdir_done()
endif

gstdshowsinkwrapper = library('gstdshowsinkwrapper',
  dshowsinkwrapper_sources,
  cpp_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [
    gstvideo_dep,
    strmbase_dep,
    winmm_dep,
    mfuuid_dep
  ],
  install : true,
  install_dir : plugins_install_dir,
  override_options : ['cpp_std=none'])

pkgconfig.generate(gstdshowsinkwrapper, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstdshowsinkwrapper]
