executable('gst-transcoder-' + api_version,
  'gst-transcoder.c', 'utils.c',
  install : true,
  install_tag : 'bin',
  dependencies : [gst_dep, gstpbutils_dep, gst_transcoder_dep],
  c_args: ['-DG_LOG_DOMAIN="gst-transcoder-@0@"'.format(api_version)],
)
