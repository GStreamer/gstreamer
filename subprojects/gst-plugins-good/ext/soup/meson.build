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

gmodule_dep = dependency('gmodule-2.0', fallback: ['glib', 'libgmodule_dep'])
gobject_dep = dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])

libdl_dep = cc.find_library('dl', required: false)

extra_args = []
extra_deps = []
if get_option('default_library') == 'static'
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
    extra_deps += libsoup3_dep
    extra_args += '-DSTATIC_SOUP=3'
  elif libsoup2_dep.found()
    extra_deps += libsoup2_dep
    extra_args += '-DSTATIC_SOUP=2'
  endif
endif

gstsouphttpsrc = library('gstsoup',
  soup_sources,
  c_args : gst_plugins_good_args + extra_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gsttag_dep, gmodule_dep, gobject_dep, gio_dep, libdl_dep] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstsouphttpsrc, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstsouphttpsrc]
