directsoundsink_sources = [
  'gstdirectsoundsink.c',
  'gstdirectsoundplugin.c',
]

if host_machine.system() == 'windows' and cc.has_header('dsound.h')
  directsoundsink_dep = [cc.find_library('dsound'), cc.find_library('winmm'), cc.find_library('ole32')]

  gstdirectsoundsink = library('gstdirectsoundsink',
    directsoundsink_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + directsoundsink_dep,
    install : true,
    install_dir : plugins_install_dir)
endif
