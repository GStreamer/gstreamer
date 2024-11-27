if not add_languages('rust', required: get_option('dots_viewer'))
  subdir_done()
endif

rustc = meson.get_compiler('rust')

cargo = find_program('cargo', version:'>=1.40', required: get_option('dots_viewer'))
if not cargo.found()
  subdir_done()
endif

cargo_wrapper = find_program('cargo_wrapper.py')
extra_env = {'RUSTC': ' '.join(rustc.cmd_array())}

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

foreach binname: ['gst-dots-viewer', 'gstdump']
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
