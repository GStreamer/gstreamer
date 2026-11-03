if not add_languages('rust', required: get_option('dots_viewer'), native: false)
  subdir_done()
endif

rustc = meson.get_compiler('rust')

cargo = find_program('cargo', version:'>=1.40', required: get_option('dots_viewer'))
if not cargo.found()
  subdir_done()
endif

cargo_wrapper = find_program('cargo_wrapper.py')
extra_env = {'RUSTC': ' '.join(rustc.cmd_array())}

if meson.is_cross_build()
  # Forward the cross toolchain to cargo build scripts (cc, pkg-config and
  # bindgen crates), which do not know anything about the Meson machine
  # files. The cc crate reads the TARGET_* variants only when building for
  # the target, so this does not affect host build scripts.
  cc_found = add_languages('c', required: false, native: false)
  cpp_found = add_languages('cpp', required: false, native: false)
  if cc_found
    cc = meson.get_compiler('c')
    extra_env += {
      'TARGET_CC': ' '.join(cc.cmd_array()),
      'TARGET_CFLAGS': ' '.join(get_option('c_args')),
    }
  endif
  if cpp_found
    cpp = meson.get_compiler('cpp')
    extra_env += {
      'TARGET_CXX': ' '.join(cpp.cmd_array()),
      'TARGET_CXXFLAGS': ' '.join(get_option('cpp_args')),
    }
  endif
  sys_root = meson.get_external_property('sys_root', '')
  if sys_root != ''
    # Consumed by cargo_wrapper.py, which knows the Rust target triple and
    # turns these into the triple-suffixed variables understood by the
    # pkg-config and bindgen crates.
    extra_env += {'MESON_CROSS_SYSROOT': sys_root}
  endif
  pkg_config_libdir = meson.get_external_property('pkg_config_libdir', '')
  if pkg_config_libdir != ''
    extra_env += {'MESON_CROSS_PKG_CONFIG_LIBDIR': pkg_config_libdir}
  endif
endif

system = host_machine.system()
exe_suffix = ''
if system == 'windows'
  exe_suffix = '.exe'
endif

if get_option('debug')
  target = 'debug'
else
  target = 'release'
endif

# Extra env to pass to cargo
if get_option('default_library') == 'static'
  extra_env += {
    # Tell the pkg-config crate to think of all libraries as static
    'PKG_CONFIG_ALL_STATIC': '1',
    # Tell the system-deps crate to process linker flag for static deps
    'SYSTEM_DEPS_LINK': 'static'
  }
endif

foreach binname: ['gst-dots-viewer']
  custom_target(binname,
    build_by_default: true,
    output: binname + exe_suffix,
    console: true,
    install: true,
    install_dir: get_option('bindir'),
    depfile: binname + '.dep',
    env: extra_env,
    command: [cargo_wrapper,
      'build',
      meson.current_build_dir(),
      meson.current_source_dir(),
      meson.global_build_root(),
      target,
      get_option('prefix'),
      get_option('libdir'),
      '--depfile', '@DEPFILE@',
      '--bin', binname,
      '--exe-suffix', exe_suffix,
    ])
endforeach

devenv = environment()
devenv.append('PATH', meson.current_build_dir())
meson.add_devenv(devenv)
