debugutilsbad_sources = [
  'debugutilsbad.c',
  'fpsdisplaysink.c',
  'gstchecksumsink.c',
  'gstchopmydata.c',
  'gstclockselect.c',
  'gstcompare.c',
  'gstdebugspy.c',
  'gsterrorignore.c',
  'gstfakeaudiosink.c',
  'gstfakesinkutils.c',
  'gstfakevideosink.c',
  'gsttestsrcbin.c',
  'gstvideocodectestsink.c',
  'gstwatchdog.c',
]

debugutilsbad_headers = [
  'gstchopmydata.h',
  'fpsdisplaysink.h',
  'gstfakeaudiosink.h',
  'gstwatchdog.h',
  'gstcompare.h',
  'gstdebugutilsbadelements.h',
  'gstfakevideosink.h',
  'gstfakesinkutils.h',
  'gstvideocodectestsink.h',
  'gstchecksumsink.h',
  'gstdebugspy.h',
  'gstclockselect.h',
  'gsterrorignore.h',
]

doc_sources = []
foreach s: debugutilsbad_sources + debugutilsbad_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'debugutilsbad': pathsep.join(doc_sources)
}

if get_option('debugutils').disabled()
  subdir_done()
endif

gstdebugutilsbad = library('gstdebugutilsbad',
  debugutilsbad_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstnet_dep, gstaudio_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdebugutilsbad]
