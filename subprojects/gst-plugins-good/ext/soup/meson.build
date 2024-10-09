soup_sources = [
  'gstsoup.c',
  'gstsoupelement.c',
  'gstsouphttpclientsink.c',
  'gstsouphttpsrc.c',
  'gstsouploader.c',
  'gstsouputils.c',
]

soup_opt = get_option('soup')
soup_ver_opt = get_option('soup-version')
if soup_opt.disabled()
  subdir_done()
endif

if soup_lookup_dep and not libsoup3_dep.found() and not libsoup2_dep.found()
  if soup_opt.enabled()
    error(f'soup: Either libsoup2 or libsoup3 is needed')
  endif
  message(f'Not building soup plugin: must link to either libsoup2 or libsoup3')
  subdir_done()
endif

libdl_dep = cc.find_library('dl', required: false)

soup_library_kwargs = {
  'sources' : soup_sources,
  'link_args' : noseh_link_args,
  'include_directories' : [configinc, libsinc],
  'install' : true,
  'install_dir' : plugins_install_dir,
}
soup_library_deps = [gst_dep, gstbase_dep, gsttag_dep, gmodule_dep, gio_dep, libdl_dep]
soup_library_c_args = gst_plugins_good_args

if host_system == 'windows'
  assert(soup_linked_target)
  gstsouphttpsrc = library('gstsoup',
    c_args : soup_library_c_args + soup_linked_target_args,
    dependencies : soup_library_deps + soup_linked_target_deps,
    kwargs: soup_library_kwargs,
  )
  gstsouphttpsrc_shared = gstsouphttpsrc
  gstsouphttpsrc_static = gstsouphttpsrc
else
  if default_library in ['static', 'both']
    assert(soup_linked_target)
    gstsouphttpsrc_static = static_library('gstsoup',
      c_args : soup_library_c_args + soup_linked_target_args,
      dependencies : soup_library_deps + soup_linked_target_deps,
      kwargs: soup_library_kwargs,
    )
  endif
  if default_library in ['shared', 'both']
    gstsouphttpsrc_shared = shared_library('gstsoup',
      c_args : soup_library_c_args,
      dependencies : soup_library_deps,
      kwargs: soup_library_kwargs + soup_dlopen_target_kwargs,
    )
  endif
endif

# Use the static library to generate the .pc file on Linux if it's available.
# In that case, the shared library .pc file does not have a Requires: on
# libsoup-2.4, and we use plugin .pc files to generate dependencies for linking
# plugins statically.
if default_library == 'shared'
  pkgconfig.generate(gstsouphttpsrc_shared, install_dir : plugins_pkgconfig_install_dir)
else
  pkgconfig.generate(gstsouphttpsrc_static, install_dir : plugins_pkgconfig_install_dir)
endif

# Add the shared library to the plugins list if available. We pass this list of
# plugins to hotdoc to generate the plugins cache, which introspects the plugin
# by loading it. We need the shared plugin for that.
if default_library == 'static'
  plugins += [gstsouphttpsrc_static]
else
  plugins += [gstsouphttpsrc_shared]
endif
