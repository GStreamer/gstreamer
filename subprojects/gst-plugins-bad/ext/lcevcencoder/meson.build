lcevcencoder_sources = [
  'plugin.c',
  'gstlcevcencoderutils.c',
  'gstlcevcencoder.c',
  'gstlcevch264enc.c',
]

lcevc_eil_dep = dependency ('lcevc_eil', required: get_option('lcevcencoder'))

if lcevc_eil_dep.found()
  gstlcevcencoder = library('gstlcevcencoder',
    lcevcencoder_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [
      gstvideo_dep,
      gstcodecparsers_dep,
      lcevc_eil_dep,
    ],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstlcevcencoder]
endif
