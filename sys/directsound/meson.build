directsoundsrc_sources = [
  'gstdirectsoundsrc.c',
  'gstdirectsounddevice.c',
  'gstdirectsoundplugin.c',
]

directsoundsrc_device_flags = [
  '-DGstDirectSoundDeviceProvider=GstDirectSoundSrcDeviceProvider',
  '-DGstDirectSoundDeviceProviderClass=GstDirectSoundSrcDeviceProviderClass',
  '-DGstDirectSoundDevice=GstDirectSoundSrcDevice',
  '-DGstDirectSoundDeviceClass=GstDirectSoundSrcDeviceClass',
  '-DGST_DIRECTSOUND_SRC_DEVICE_PROVIDER=1'
]

if host_system == 'windows' and cc.has_header('dsound.h')
  directsoundsrc_dep = [cc.find_library('dsound'), cc.find_library('winmm'), cc.find_library('ole32')]

  gstdirectsoundsrc = library('gstdirectsoundsrc',
    directsoundsrc_sources,
    c_args : gst_plugins_bad_args + directsoundsrc_device_flags,
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + directsoundsrc_dep,
    install : true,
    install_dir : plugins_install_dir)
endif
