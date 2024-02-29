soundtouch_sources = [
  'plugin.c',
  'gstpitch.cc',
  'gstbpmdetect.cc',
]

soundtouch_cargs = ['-DHAVE_SOUNDTOUCH']
if get_option('soundtouch').disabled()
  subdir_done()
endif

soundtouch_dep = dependency('soundtouch', required : false,
                                          fallback : ['soundtouch', 'soundtouch_dep'])
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
if not soundtouch_dep.found() and get_option('soundtouch').enabled()
  error('soundtouch plugin enabled but soundtouch library not found')
endif

# GCC, by default, handles exceptions in C++ sources as /EHsc, allowing
# exceptions intra C++ code, and terminating the app on hitting a C function's
# stack.
# Grep ST_NO_EXCEPTION_HANDLING and see:
# https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170
# https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html#index-fexceptions
#
# However, -fexceptions in Application.mk must be manually injected, as
# their toolchain does not add it for historical reasons.
# https://developer.android.com/ndk/guides/cpp-support?hl=es-419
if get_option('cpp_eh') in ['none']
  soundtouch_cargs += ['-DST_NO_EXCEPTION_HANDLING']
elif host_system == 'android'
  soundtouch_cargs += cxx.get_supported_arguments('-fexceptions')
endif

if soundtouch_dep.found()
  gstsoundtouch = library('gstsoundtouch',
    soundtouch_sources,
    c_args : gst_plugins_bad_args + soundtouch_cargs,
    cpp_args : gst_plugins_bad_args + soundtouch_cargs,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, soundtouch_dep],
    override_options: ['cpp_std=c++14'],
    install : true,
    install_dir : plugins_install_dir)
  plugins += [gstsoundtouch]
endif
