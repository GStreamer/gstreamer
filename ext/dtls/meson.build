dtls_sources = [
  'gstdtlsagent.c',
  'gstdtlscertificate.c',
  'gstdtlsconnection.c',
  'gstdtlsdec.c',
  'gstdtlsenc.c',
  'gstdtlssrtpbin.c',
  'gstdtlssrtpdec.c',
  'gstdtlssrtpdemux.c',
  'gstdtlssrtpenc.c',
  'plugin.c',
]

libcrypto_dep = dependency('libcrypto', required : false)

if openssl_dep.found() and libcrypto_dep.found()
  gstdtls = library('gstdtls',
    dtls_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gst_dep, libcrypto_dep, openssl_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
