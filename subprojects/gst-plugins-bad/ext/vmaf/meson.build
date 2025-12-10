vmaf_sources = [
  'gstvmafelement.c',
  'gstvmafplugin.c',
]

vmaf_headers = [
  'gstvmafenums.h',
  'gstvmafelement.h',
]

doc_sources = []
foreach s: vmaf_sources + vmaf_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'vmaf': pathsep.join(doc_sources)
}

if get_option('vmaf').disabled()
  libvmaf_dep = dependency('', required: false)
  subdir_done()
endif

cc = meson.get_compiler('c')
is_msvc_windows = host_system == 'windows' and cc.get_id() == 'msvc'

libvmaf_dep = dependency('libvmaf', 
  required : get_option('vmaf'), 
  fallback : is_msvc_windows ? [] : ['libvmaf', 'libvmaf_dep'])

if not libvmaf_dep.found()
  subdir_done()
endif

gstvmaf = library('gstvmaf',
    vmaf_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, libvmaf_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstvmaf, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstvmaf]
