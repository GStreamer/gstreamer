tool_deps = glib_deps + [pbutils_dep, audio_dep, video_dep, tag_dep, gst_dep, gst_base_dep, gmodule_dep]

extra_args = []
extra_deps = []

if host_system == 'windows'
  # Check whether we're building for UWP apps, and if so, will not link winmm
  # of which APIs are for WIN32 desktop
  building_for_uwp = false
  code = '''
  #include <windows.h>
  #if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP))
  #error "Not building for UWP"
  #endif'''
  if cc.compiles(code, name : 'building for UWP')
    building_for_uwp = true
  endif

  if not building_for_uwp
    winmm_lib = cc.find_library('winmm', required: false)
    if winmm_lib.found() and cc.has_header('mmsystem.h')
      extra_args += ['-DHAVE_WINMM']
      extra_deps += [winmm_lib]
    endif
  endif
endif

executable('gst-device-monitor-@0@'.format(api_version),
  'gst-device-monitor.c',
  install: true,
  c_args : gst_plugins_base_args + ['-DG_LOG_DOMAIN="gst-device-monitor-@0@"'.format(api_version)],
  include_directories: [configinc],
  dependencies : tool_deps,
)
install_man('gst-device-monitor-@0@.1'.format(api_version))

executable('gst-discoverer-@0@'.format(api_version),
  'gst-discoverer.c',
  install: true,
  c_args : gst_plugins_base_args + ['-DG_LOG_DOMAIN="gst-discoverer-@0@"'.format(api_version)],
  include_directories: [configinc],
  dependencies : tool_deps,
)
install_man('gst-discoverer-@0@.1'.format(api_version))

executable('gst-play-@0@'.format(api_version),
  'gst-play.c', 'gst-play-kb.c',
  install: true,
  c_args : gst_plugins_base_args + ['-DG_LOG_DOMAIN="gst-play-@0@"'.format(api_version)] + extra_args,
  include_directories: [configinc],
  dependencies : tool_deps + [libm] + extra_deps,
)
install_man('gst-play-@0@.1'.format(api_version))
