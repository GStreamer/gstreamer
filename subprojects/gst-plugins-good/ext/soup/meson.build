soup_sources = [
  'gstsoup.c',
  'gstsoupelement.c',
  'gstsouphttpclientsink.c',
  'gstsouphttpsrc.c',
  'gstsouploader.c',
  'gstsouputils.c',
]

soup_opt = get_option('soup')
if soup_opt.disabled()
  subdir_done()
endif

libdl_dep = cc.find_library('dl', required: false)

static_args = []
static_deps = []
default_library = get_option('default_library')
if default_library in ['static', 'both'] or get_option('soup-lookup-dep')
  libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48',
                            required : false, fallback : ['libsoup', 'libsoup_dep'],
                            default_options: ['sysprof=disabled'])
  libsoup3_dep = dependency('libsoup-3.0', required : false,
                            fallback : ['libsoup3', 'libsoup_dep'])
  if not libsoup2_dep.found() and not libsoup3_dep.found()
    if soup_opt.enabled()
      error('Either libsoup2 or libsoup3 is needed')
    endif
    subdir_done()
  endif
  if libsoup3_dep.found()
    static_deps += libsoup3_dep
    static_args += '-DSTATIC_SOUP=3'
    message('soup plugin: using libsoup-3.0 for static build')
  elif libsoup2_dep.found()
    static_deps += libsoup2_dep
    static_args += '-DSTATIC_SOUP=2'
    message('soup plugin: using libsoup-2.4 for static build')
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

if default_library in ['shared', 'both']
  gstsouphttpsrc_shared = shared_library('gstsoup',
    c_args : soup_library_c_args,
    dependencies : soup_library_deps,
    kwargs: soup_library_kwargs,
  )
endif

if default_library in ['static', 'both']
  gstsouphttpsrc_static = static_library('gstsoup',
    c_args : soup_library_c_args + static_args,
    dependencies : soup_library_deps + static_deps,
    kwargs: soup_library_kwargs,
  )
endif

# Use the static library to generate the .pc file if it's available. The shared
# library .pc file does not have a Requires: on libsoup-2.4, and we use plugin
# .pc files to generate dependencies for linking plugins statically.
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
