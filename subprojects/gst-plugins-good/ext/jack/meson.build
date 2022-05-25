jack_sources = [
  'gstjackaudioclient.c',
  'gstjackaudiosink.c',
  'gstjackaudiosrc.c',
  'gstjack.c',
  'gstjackutil.c',
]

jack_option = get_option('jack')
if jack_option.disabled()
  subdir_done()
endif

jack_incdirs = [configinc, libsinc]

libjack_dep = dependency('jack', version : '>= 1.9.7', required : false)

if not libjack_dep.found()
  fs = import('fs')
  host_cpu = host_machine.cpu_family()
  jack_maybe_installed = false
  error_msg = '"jack" option is enabled but '
  if (host_system == 'windows' and build_machine.system() == 'windows')
    # Need to detect whether we're running on 64-bit Windows or not.
    # If `C:/Program Files (x86)/` exists, we're running on 64-bit Windows, and
    # C:/Program Files/ contains 64-bit programs. Else, we're on 32-bit Windows
    # and C:/Program Files/ contains 32-bit programs.
    #
    # The user could either have a 32-bit JACK installation or a 64-bit one.
    # When building for 32-bit x86, we need to check for both.
    if fs.is_dir('C:/Program Files (x86)')
      jack64_install_dir = 'C:/Program Files/JACK2'
      jack32_install_dir = 'C:/Program Files (x86)/JACK2'
    else
      jack64_install_dir = ''
      jack32_install_dir = 'C:/Program Files/JACK2'
    endif

    if host_cpu == 'x86'
      jack_install_dir = jack32_install_dir
      jack_maybe_installed = fs.is_dir(jack32_install_dir / 'include')
      if not jack_maybe_installed and jack64_install_dir != ''
        jack_maybe_installed = fs.is_dir(jack64_install_dir / 'include')
        jack_install_dir = jack64_install_dir
      endif
    elif jack64_install_dir != ''
      jack_maybe_installed = import('fs').is_dir(jack64_install_dir / 'include')
      jack_install_dir = jack64_install_dir
    endif

    error_msg += 'JACK2 installation could not be found'
  else
    error_msg += 'JACK dependency could not be found'
  endif

  if not jack_maybe_installed
    if jack_option.enabled()
      error(error_msg)
    endif
    subdir_done()
  endif

  if not host_cpu.startswith('x86')
    if jack_option.enabled()
      error('On Windows, JACK only supports x86 32-bit and 64-bit')
    endif
    subdir_done()
  endif

  if host_cpu == 'x86'
    jack_libname = 'libjack'
    if jack_install_dir == jack64_install_dir
      jack_libdir = jack_install_dir / 'lib32'
    else
      jack_libdir = jack_install_dir / 'lib'
    endif
  else
    jack_libname = 'libjack64'
    jack_libdir = jack_install_dir / 'lib'
  endif

  inc = include_directories(jack_install_dir / 'include')
  libjack_dep = cc.find_library(jack_libname,
      dirs: jack_libdir,
      has_headers: 'jack/jack.h',
      header_include_directories: inc,
      required: jack_option)
  # This won't be needed once we require a meson version that includes this:
  # https://github.com/mesonbuild/meson/pull/10428
  jack_incdirs += inc
endif

gstjack = library('gstjack',
  jack_sources,
  c_args : gst_plugins_good_args + ['-DHAVE_JACK_1_9_7'],
  include_directories : jack_incdirs,
  dependencies : [gst_dep, gstbase_dep, gstaudio_dep, libjack_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstjack, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstjack]
