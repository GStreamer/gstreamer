wasapi2_sources = [
  'gstwasapi2src.c',
  'gstwasapi2sink.c',
  'gstwasapi2util.c',
  'gstwasapi2client.cpp',
  'gstwasapi2device.c',
  'plugin.c',
]

mmdeviceapi_symbols = [
  'ActivateAudioInterfaceAsync',
  'DEVINTERFACE_AUDIO_RENDER',
  'DEVINTERFACE_AUDIO_CAPTURE',
]

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
runtimeobject_dep = cc.find_library('runtimeobject', required : get_option('wasapi2'))
mmdeviceapi_dep = cc.find_library('mmdevapi', required : get_option('wasapi2'))
wasapi2_dep = [ole32_dep, ksuser_dep, runtimeobject_dep, mmdeviceapi_dep]
have_symbols = false

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

winapi_app = cxx.compiles('''#include <winapifamily.h>
  #include <windows.applicationmodel.core.h>
  #include <wrl.h>
  #include <wrl/wrappers/corewrappers.h>
  #include <audioclient.h>
  #include <mmdeviceapi.h>
  #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP)
  #error "not winrt"
  #endif
  int main (int argc, char ** argv) {
    IAudioClient3 *client = NULL;
    return 0;
  } ''',
  dependencies: wasapi2_dep,
  name: 'checking if building winapi-partiion-app')

if not winapi_app
  if wasapi2_option.enabled()
    error('wasapi2 plugin was enabled explicitly, but build target is not include WINAPI_PARTITION_APP')
  else
    subdir_done()
  endif
endif

gstwasapi2 = library('gstwasapi2',
  wasapi2_sources,
  c_args : gst_plugins_bad_args + ['-DCOBJMACROS'],
  cpp_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep] + wasapi2_dep,
  install : true,
  install_dir : plugins_install_dir)
pkgconfig.generate(gstwasapi2, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstwasapi2]
