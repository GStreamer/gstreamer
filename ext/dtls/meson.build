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

openssl_dep = dependency('openssl', version : '>= 1.0.1', required : get_option('dtls'))
libcrypto_dep = dependency('libcrypto', required : get_option('dtls'))

if openssl_dep.found() and libcrypto_dep.found()
  gstdtls = library('gstdtls',
    dtls_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gst_dep, libcrypto_dep, openssl_dep] + winsock2,
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstdtls, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstdtls]
endif
