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

libdl_dep = cc.find_library('dl', required: false)

soup_link_args = []
soup_link_deps = []
libsoup2_dep = disabler()
libsoup3_dep = disabler()
default_library = get_option('default_library')
soup_lookup_dep = get_option('soup-lookup-dep') and host_system == 'linux'
if host_system != 'linux' or default_library in ['static', 'both'] or soup_lookup_dep
  if soup_ver_opt in ['auto', '3']
    libsoup3_dep = dependency('libsoup-3.0', allow_fallback: true,
                              required: soup_ver_opt == '3' and soup_opt.enabled())
  endif
  if soup_ver_opt in ['auto', '2']
    libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48', allow_fallback: true,
                              default_options: ['sysprof=disabled'],
                              required: soup_ver_opt == '2' and soup_opt.enabled())
  endif
endif

if host_system != 'linux' or default_library in ['static', 'both']
  if libsoup3_dep.found()
    soup_link_deps += libsoup3_dep
    soup_link_args += '-DLINK_SOUP=3'
    message('soup plugin: linking to libsoup-3.0')
  elif libsoup2_dep.found()
    soup_link_deps += libsoup2_dep
    soup_link_args += '-DLINK_SOUP=2'
    message('soup plugin: linking to libsoup-2.4')
  else
    if soup_opt.enabled()
      error('Either libsoup2 or libsoup3 is needed')
    endif
    subdir_done()
  endif
endif

soup_library_kwargs = {
  'sources' : soup_sources,
  'link_args' : noseh_link_args,
  'include_directories' : [configinc, libsinc],
  'install' : true,
  'install_dir' : plugins_install_dir,
}
soup_library_deps = [gst_dep, gstbase_dep, gsttag_dep, gmodule_dep, gio_dep, libdl_dep]
soup_library_c_args = gst_plugins_good_args

if host_system != 'linux'
  gstsouphttpsrc = library('gstsoup',
    c_args : soup_library_c_args + soup_link_args,
    dependencies : soup_library_deps + soup_link_deps,
    kwargs: soup_library_kwargs,
  )
  gstsouphttpsrc_shared = gstsouphttpsrc
  gstsouphttpsrc_static = gstsouphttpsrc
else
  if default_library in ['static', 'both']
    gstsouphttpsrc_static = static_library('gstsoup',
      c_args : soup_library_c_args + soup_link_args,
      dependencies : soup_library_deps + soup_link_deps,
      kwargs: soup_library_kwargs,
    )
  endif
  if default_library in ['shared', 'both']
    gstsouphttpsrc_shared = shared_library('gstsoup',
      c_args : soup_library_c_args,
      dependencies : soup_library_deps,
      kwargs: soup_library_kwargs,
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
