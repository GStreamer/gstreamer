directsoundsink_sources = [
  'gstdirectsoundsink.c',
  'gstdirectsounddevice.c',
  'gstdirectsoundplugin.c',
]

directsoundsink_headers = [
  'gstdirectsounddevice.h',
  'gstdirectsoundsink.h',
]

directsoundsink_device_flags = [
  '-DGstDirectSoundDeviceProvider=GstDirectSoundSinkDeviceProvider',
  '-DGstDirectSoundDeviceProviderClass=GstDirectSoundSinkDeviceProviderClass',
  '-DGstDirectSoundDevice=GstDirectSoundSinkDevice',
  '-DGstDirectSoundDeviceClass=GstDirectSoundSinkDeviceClass',
]

doc_sources = []
foreach s: directsoundsink_sources + directsoundsink_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'directsound': pathsep.join(doc_sources)
}

have_dsound = false
dsound_option = get_option('directsound')
if host_system != 'windows' or dsound_option.disabled()
  subdir_done()
endif

# TODO: https://github.com/mesonbuild/meson/issues/3940
have_dsound = cc.has_header('dsound.h')
if not have_dsound and dsound_option.enabled()
  error('directsound plugin was enabled but dsound.h was not found')
endif

if have_dsound
  directsoundsink_dep = [cc.find_library('dsound'), cc.find_library('winmm'), cc.find_library('ole32')]

  gstdirectsoundsink = library('gstdirectsound',
    directsoundsink_sources,
    c_args : gst_plugins_good_args + directsoundsink_device_flags,
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + directsoundsink_dep,
    install : true,
    install_dir : plugins_install_dir)
  plugins += [gstdirectsoundsink]
endif
