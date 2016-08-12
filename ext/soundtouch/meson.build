soundtouch_sources = [
  'plugin.c',
  'gstpitch.cc',
  'gstbpmdetect.cc',
]

soundtouch_cargs = ['-DHAVE_SOUNDTOUCH']

soundtouch_dep = dependency('soundtouch', required : false)
if soundtouch_dep.found()
  soundtouch_cargs += ['-DHAVE_SOUNDTOUCH_1_4']
else
  soundtouch_dep = dependency('soundtouch-1.4', required : false)
  if soundtouch_dep.found()
    soundtouch_cargs += ['-DHAVE_SOUNDTOUCH_1_4']
  else
    soundtouch_dep = dependency('soundtouch-1.0', required : false)
    # NOTE: I removed the checks for libSoundTouch.pc and so on.
    # Add them back once we know which distros use them.
  endif
endif

if soundtouch_dep.found()
  gstsoundtouch = library('gstsoundtouch',
    soundtouch_sources,
    c_args : gst_plugins_bad_args + soundtouch_cargs,
    cpp_args : gst_plugins_bad_args + soundtouch_cargs,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, soundtouch_dep],
    install : true,
    install_dir : plugins_install_dir)
endif
