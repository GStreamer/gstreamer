wasapi2_sources = [
  'gstwasapi2src.c',
  'gstwasapi2sink.c',
  'gstwasapi2util.cpp',
  'gstwasapi2device.cpp',
  'gstwasapi2ringbuffer.cpp',
  'gstwasapi2activator.cpp',
  'gstwasapi2enumerator.cpp',
  'gstwasapi2object.cpp',
  'plugin.cpp',
]

wasapi2_headers = [
  'gstwasapi2ringbuffer.h',
  'gstwasapi2device.h',
  'gstwasapi2util.h',
  'gstwasapi2src.h',
  'gstwasapi2sink.h',
  'gstwasapi2object.h',
]

mmdeviceapi_symbols = [
  'ActivateAudioInterfaceAsync',
  'DEVINTERFACE_AUDIO_RENDER',
  'DEVINTERFACE_AUDIO_CAPTURE',
]

doc_sources = []
foreach s: wasapi2_sources + wasapi2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'wasapi2': pathsep.join(doc_sources)
}

wasapi2_option = get_option('wasapi2')
if host_system != 'windows'
  if wasapi2_option.disabled()
    subdir_done()
  elif wasapi2_option.enabled()
    error('Cannot build wasapi2 plugin when not building for Windows')
  endif
endif

ole32_dep = cc.find_library('ole32', required : get_option('wasapi2'))
ksuser_dep = cc.find_library('ksuser', required : get_option('wasapi2'))
mmdeviceapi_dep = cc.find_library('mmdevapi', required : get_option('wasapi2'))
mfplat_dep = cc.find_library('mfplat', required : get_option('wasapi2'))
wasapi2_dep = [ole32_dep, ksuser_dep, mmdeviceapi_dep, mfplat_dep]
extra_args = []

foreach dep: wasapi2_dep
  if not dep.found()
    if wasapi2_option.enabled()
      error('wasapi2 plugin was enabled explicitly, but required dependencies were not found')
    else
      subdir_done()
    endif
  endif
endforeach

if not cxx.has_header_symbol ('audioclient.h', 'IAudioClient3', dependencies : wasapi2_dep)
  if wasapi2_option.enabled()
    error('wasapi2 plugin was enabled explicitly, but IAudioClient3 is unavailable')
  else
    subdir_done()
  endif
endif

foreach symbol: mmdeviceapi_symbols
  if not cxx.has_header_symbol ('mmdeviceapi.h', symbol, dependencies : wasapi2_dep)
    if wasapi2_option.enabled()
      error('wasapi2 plugin was enabled explicitly, but @1@ is unavailable'.format(symbol))
    else
      subdir_done()
    endif
  endif
endforeach

win10_sdk = cxx.compiles('''#include <windows.h>
    #ifndef WDK_NTDDI_VERSION
    #error "unknown Windows SDK version"
    #endif
    #if (WDK_NTDDI_VERSION < 0x0A000000)
    #error "Not a Windows 10 SDK"
    #endif
    ''',
    name: 'building with Windows 10 SDK')

if not win10_sdk
  if wasapi2_option.enabled()
    error('wasapi2 plugin was enabled explicitly, but Windows 10 SDK is unavailable')
  else
    subdir_done()
  endif
endif

building_for_win10 = cxx.compiles('''#include <windows.h>
    #ifndef WINVER
    #error "unknown minimum supported OS version"
    #endif
    #if (WINVER < 0x0A00)
    #error "Windows 10 API is not guaranteed"
    #endif
    ''',
    name: 'building for Windows 10')

if not building_for_win10
  message('Bumping target Windows version to Windows 10 for building wasapi2 plugin')
  extra_args += ['-U_WIN32_WINNT', '-UWINVER', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-DNTDDI_VERSION=WDK_NTDDI_VERSION']
endif

gstwasapi2 = library('gstwasapi2',
  wasapi2_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep] + wasapi2_dep,
  override_options : ['cpp_std=c++14'],
  install : true,
  install_dir : plugins_install_dir)
plugins += [gstwasapi2]
