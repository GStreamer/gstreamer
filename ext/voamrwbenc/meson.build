voamrwbenc_dep = dependency('vo-amrwbenc', version: '>= 0.1.0', required: get_option('voamrwbenc'))

if voamrwbenc_dep.found()
  gstvoamrwbenc = library('gstvoamrwbenc',
    'gstvoamrwb.c', 'gstvoamrwbenc.c',
    c_args: gst_plugins_bad_args,
    link_args: noseh_link_args,
    include_directories: [configinc],
    dependencies: [gstaudio_dep, gstpbutils_dep, gsttag_dep, voamrwbenc_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstvoamrwbenc, install_dir: plugins_pkgconfig_install_dir)

  install_data('GstVoAmrwbEnc.prs', install_dir: presetdir)
endif
