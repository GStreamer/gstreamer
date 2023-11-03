decklink2_sources = [
  'gstdecklink2combiner.cpp',
  'gstdecklink2demux.cpp',
  'gstdecklink2deviceprovider.cpp',
  'gstdecklink2input.cpp',
  'gstdecklink2device.cpp',
  'gstdecklink2output.cpp',
  'gstdecklink2sink.cpp',
  'gstdecklink2sinkbin.cpp',
  'gstdecklink2src.cpp',
  'gstdecklink2srcbin.cpp',
  'gstdecklink2utils.cpp',
  'plugin.cpp',
]

decklink2_headers = [
  'gstdecklink2combiner.h',
  'gstdecklink2demux.h',
  'gstdecklink2deviceprovider.h',
  'gstdecklink2input.h',
  'gstdecklink2device.h',
  'gstdecklink2output.h',
  'gstdecklink2sink.h',
  'gstdecklink2sinkbin.h',
  'gstdecklink2sinkprops.h',
  'gstdecklink2src.h',
  'gstdecklink2srcbin.h',
  'gstdecklink2utils.h',
]

doc_sources = []
foreach s: decklink2_sources + decklink2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'decklink2': pathsep.join(doc_sources)
}

decklink2_sdk = []
decklink2_deps = []
decklink2_extra_args = ['-DGST_USE_UNSTABLE_API']
decklink2_ldflags = []
decklink2_incl = []
decklink2_option = get_option('decklink2')

if decklink2_option.disabled()
  subdir_done()
endif

# Build SDK headers using midl compiler on Windows
if host_system == 'windows'
  midl = find_program('midl', required: decklink2_option)
  if not midl.found()
    subdir_done()
  endif

  if host_machine.cpu_family() == 'x86'
    midl_env = 'win32'
  elif host_machine.cpu_family() == 'x86_64'
    midl_env = 'win64'
  else
    # ARM64 support?
    if decklink2_option.enabled()
      error(host_machine.cpu_family() + ' is not supported')
    endif
    subdir_done()
  endif

  decklink2_sdk = custom_target('DeckLinkAPI.h',
      output : ['DeckLinkAPI_i.c', 'DeckLinkAPI.h'],
      input : 'win/DeckLinkAPI.idl',
      command : [midl, '/h', 'DeckLinkAPI.h',
                '/env', midl_env,
                '/target', 'NT50',
                '/notlb',
                '/out', meson.current_build_dir(),
                '@INPUT@']
  )
else
  libdl = cc.find_library('dl', required: decklink2_option)
  have_pthread_h = cc.has_header('pthread.h', required: decklink2_option)
  if not libdl.found() or not have_pthread_h
    subdir_done()
  endif

  if cc.has_function('posix_memalign', prefix : '#include <stdlib.h>')
    decklink2_extra_args += ['-DHAVE_POSIX_MEMALIGN']
  endif

  decklink2_extra_args += cc.get_supported_arguments([
    '-Wno-missing-declarations',
  ])

  decklink2_deps += [libdl, dependency('threads')]
  if host_system == 'linux'
    decklink2_sdk += ['linux/DeckLinkAPIDispatch.cpp',
                      'linux/DeckLinkAPIDispatch_v10_11.cpp']
    decklink2_incl = include_directories('linux')
  elif host_system in ['darwin', 'ios']
    decklink2_sdk += ['mac/DeckLinkAPIDispatch.cpp',
                      'mac/DeckLinkAPIDispatch_v10_11.cpp']
    decklink2_ldflags += ['-Wl,-framework,CoreFoundation']
    decklink2_incl = include_directories('mac')
  else
    if decklink2_option.enabled()
      error('Host system "@0@" is not supported'.format(host_system))
    endif
    subdir_done()
  endif
endif

decklink2 = library('gstdecklink2',
    decklink2_sources + decklink2_sdk,
    c_args : gst_plugins_bad_args + decklink2_extra_args,
    cpp_args : gst_plugins_bad_args + decklink2_extra_args,
    link_args : decklink2_ldflags + noseh_link_args,
    include_directories : [configinc] + decklink2_incl,
    dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep] + decklink2_deps,
    override_options : ['cpp_std=c++14'],
    install : true,
    install_dir : plugins_install_dir,
)

plugins += [decklink2]