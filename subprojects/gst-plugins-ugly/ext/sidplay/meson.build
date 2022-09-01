sidplay_option = get_option('sidplay').require(gpl_allowed, error_message: '''
  Plugin sidplay explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

if not add_languages('cpp', native: false, required: sidplay_option)
  subdir_done()
endif

if not cxx.has_header('sidplay/player.h', required: sidplay_option)
  subdir_done()
endif

sidplay_dep = cxx.find_library('sidplay', required: sidplay_option)
if not sidplay_dep.found()
  subdir_done()
endif

# sidplay plugin works with libsidplay 1.36.x (not 2.x.x)
sid_code = '''#include <sidplay/player.h>
              void somefunc (void) {
                sidTune tune = sidTune(0);
              }'''
if not cxx.compiles(sid_code, dependencies: sidplay_dep, name : 'sidplay usage')
  if sidplay_option.enabled()
    error('sidplay headers and libraries were found, but were not usable')
  endif
  subdir_done()
endif

gstsid = library('gstsid', 'gstsiddec.cc',
  cpp_args : ugly_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep, sidplay_dep],
  install : true,
  install_dir : plugins_install_dir)
plugins += [gstsid]
