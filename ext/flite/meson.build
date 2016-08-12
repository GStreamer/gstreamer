flite_sources = [
  'gstflite.c',
  'gstflitetestsrc.c',
]

flite_libs = [
  'flite',
  'flite_cmu_us_kal',
  'flite_usenglish',
  'flite_cmulex'
]

flite_deps = [ ]
foreach flite_lib : flite_libs
  flite_lib_dep = cc.find_library(flite_lib, required : false)
  if flite_lib_dep.found()
    flite_deps += [flite_lib_dep]
  endif
endforeach

if flite_deps.length() == flite_libs.length() and cc.has_header_symbol('flite/flite.h', 'flite_init')
  gstflite = library('gstflite',
    flite_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + flite_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
