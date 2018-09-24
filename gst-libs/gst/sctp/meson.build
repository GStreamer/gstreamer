sctp_sources = [
  'sctpreceivemeta.c',
  'sctpsendmeta.c',
]

sctp_headers = [
  'sctpreceivemeta.h',
  'sctpsendmeta.h',
  'sctp-prelude.h',
]

install_headers(sctp_headers, subdir : 'gstreamer-1.0/gst/sctp')

libgstsctp = library('gstsctp-' + api_version,
  sctp_sources,
  c_args : gst_plugins_bad_args + ['-DBUILDING_GST_SCTP'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstbase_dep],
)

gstsctp_dep = declare_dependency(link_with : libgstsctp,
  include_directories : [libsinc],
  dependencies : [gstbase_dep])
