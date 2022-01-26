dshowsrcwrapper_sources = [
  'dshowdeviceprovider.cpp',
  'gstdshow.cpp',
  'gstdshowfakesink.cpp',
  'gstdshowaudiosrc.cpp',
  'gstdshowvideosrc.cpp',
  'gstdshowsrcwrapper.cpp'
]

if not strmbase_dep.found()
  message('strmbase not found, not building dshowsrcwrapper')
  subdir_done()
endif

winmm_dep = cxx.find_library('winmm', required: get_option('directshow'))
if not winmm_dep.found()
  message('winmm not found, not building dshowsrcwrapper')
  subdir_done()
endif

rpcrt4_dep = cxx.find_library('rpcrt4', required: get_option('directshow'))
if not rpcrt4_dep.found()
  message('rpcrt4 not found, not building dshowsrcwrapper')
  subdir_done()
endif

gstdshowsrcwrapper = library('gstdshowsrcwrapper',
  dshowsrcwrapper_sources,
  cpp_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [
    gstaudio_dep,
    gstvideo_dep,
    strmbase_dep,
    winmm_dep,
    rpcrt4_dep
  ],
  install : true,
  install_dir : plugins_install_dir,
  override_options : ['cpp_std=none'])

pkgconfig.generate(gstdshowsrcwrapper, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstdshowsrcwrapper]
