if get_option('jack').disabled()
  subdir_done()
endif

jack_incdirs = [configinc, libsinc]

# While we are dlopening jack for the gstjack plugins, or the example
# we should link against jack and use its api directly as that's the
# usage we expect for applications.
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

executable('jack_client', 'jack_client.c',
  dependencies: [gst_dep, gtk_dep, libjack_dep],
  c_args: gst_plugins_good_args,
  include_directories: jack_incdirs,
  install: false)
