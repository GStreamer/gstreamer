if host_system == 'windows'
  # Check whether we're building for UWP apps
  code = '''
  #include <windows.h>
  #if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP))
  #error "Not building for UWP"
  #endif'''
  if cc.compiles(code, name : 'building for UWP')
    if get_option('directshow').enabled()
      error('directshow plugins cannot be built for UWP')
    endif
    subdir_done()
  endif
endif

if cxx.get_id() != 'msvc' or get_option('directshow').disabled()
  if get_option('directshow').enabled()
    error('directshow plugins can only be built with MSVC')
  endif
  subdir_done()
endif

subdir('strmbase')
subdir('dshowdecwrapper')
subdir('dshowsrcwrapper')
subdir('dshowsinkwrapper')
