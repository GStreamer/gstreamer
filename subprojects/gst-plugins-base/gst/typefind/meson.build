# The typefinders are implemented either in the C sources (the default) or in
# the rust crate, selected with the typefind_rust option
# (-Dgst-plugins-base:typefind_rust=true in the mono-repo build).
typefindfunctions_extra_defs = []
typefindfunctions_extra_deps = []
typefindfunctions_headers = []

if get_option('typefind_rust')
  typefindfunctions_sources = [
    'gsttypefindfunctionsplugin_rs.c',
  ]
else
  if not get_option('gio-typefinder').disabled()
    typefindfunctions_extra_defs += ['-DHAVE_GIO']
    typefindfunctions_extra_deps += [gio_dep]
  endif

  typefindfunctions_sources = [
    'gsttypefindfunctions.c',
    'gsttypefindfunctionsdata.c',
    'gsttypefindfunctionsplugin.c',
    'gsttypefindfunctionsriff.c',
    'gsttypefindfunctionsstartwith.c',
  ]

  typefindfunctions_headers = [
    'gsttypefindfunctionsdata.h',
    'gsttypefindfunctionsplugin.h',
  ]
endif

doc_sources = []
foreach s: typefindfunctions_sources + typefindfunctions_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'typefindfunctions': pathsep.join(doc_sources)
}

if get_option('typefind').disabled()
  subdir_done()
endif

if get_option('typefind_rust')
  # Cargo.toml in rust/ exists only for rust-analyzer; rust is required
  # whenever the rust implementation is selected.
  add_languages('rust', native : false, required : true)
  rustc = meson.get_compiler('rust')

  # 1.88 for let chains, used throughout the finders.
  rust_req = '1.88'
  if rustc.get_id() not in ['rustc', 'clippy-driver rustc']
    warning('Typefinders are only tested with rustc, found different compiler @0@ @1@'.format(rustc.get_id(), rustc.version()))
  elif not rustc.version().version_compare('>=' + rust_req)
    error('Requires at least Rust @0@ @1@'.format(rust_req, rustc.version()))
  endif

  if get_option('gio-typefinder').enabled()
    warning('gio-typefinder only applies to the C typefinders')
  endif

  # only the crate root is passed to rustc; modules are tracked via depfile
  typefindfunctions_rs = static_library('typefindfunctions_rs',
    sources : 'rust/src/lib.rs',
    override_options : ['rust_std=2021'],
    rust_abi : 'c',
    dependencies : [gst_dep, pbutils_dep],
  )

  # unit tests
  if get_option('tests').allowed()
    typefindfunctions_rs_test = executable('typefindfunctions_rs_test',
      sources : 'rust/src/lib.rs',
      override_options : ['rust_std=2021'],
      rust_args : ['--test'],
      dependencies : [gst_dep, pbutils_dep],
    )
    test('typefindfunctions_rs', typefindfunctions_rs_test, protocol : 'rust')
  endif

  gsttypefind = library('gsttypefindfunctions',
    typefindfunctions_sources,
    c_args : gst_plugins_base_args,
    include_directories: [configinc, libsinc],
    dependencies : [pbutils_dep, gst_base_dep],
    link_with : typefindfunctions_rs,
    install : true,
    install_dir : plugins_install_dir,
  )
else
  gsttypefind = library('gsttypefindfunctions',
    typefindfunctions_sources,
    c_args : gst_plugins_base_args + typefindfunctions_extra_defs,
    include_directories: [configinc, libsinc],
    dependencies : [pbutils_dep, gst_base_dep, typefindfunctions_extra_deps],
    install : true,
    install_dir : plugins_install_dir,
  )
endif

plugins += [gsttypefind]
