decklink_sources = [
  'gstdecklink.cpp',
  'gstdecklinkaudiosink.cpp',
  'gstdecklinkvideosink.cpp',
  'gstdecklinkaudiosrc.cpp',
  'gstdecklinkvideosrc.cpp',
]

build_decklink = false
decklink_ldflags = []

libdl = cc.find_library('dl', required: false)
have_pthread_h = cdata.has('HAVE_PTHREAD_H')

if libdl.found() and have_pthread_h
  if host_machine.system() == 'linux'
    decklink_sources += ['linux/DeckLinkAPIDispatch.cpp']
    build_decklink = true
  elif host_machine.system() == 'darwin'
    decklink_sources += ['osx/DeckLinkAPIDispatch.cpp']
    decklink_ldflags = '-Wl,-framework,CoreFoundation'
    build_decklink = true
  elif host_machine.system() == 'windows'
    decklink_sources += ['win/DeckLinkAPIDispatch.cpp', 'win/DeckLinkAPI_i.c']
    decklink_ldflags = '-Wl,-framework,CoreFoundation'
    build_decklink = true
  else
    message('Not building decklink plugin for system ' + host_machine.system())
  endif
endif

if build_decklink
  decklink = library('gstdecklink',
    decklink_sources,
    cpp_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
    link_args : decklink_ldflags + noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep, libm, libdl, dependency('threads')],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
