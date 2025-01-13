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
  'gstdtlselement.c',
]

dtls_headers = [
  'gstdtlscertificate.h',
  'gstdtlsenc.h',
  'gstdtlssrtpenc.h',
  'gstdtlselements.h',
  'gstdtlssrtpdec.h',
  'gstdtlsconnection.h',
  'gstdtlsdec.h',
  'gstdtlssrtpbin.h',
  'gstdtlssrtpdemux.h',
  'gstdtlsagent.h',
]

doc_sources = []
foreach s: dtls_sources + dtls_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dtls': pathsep.join(doc_sources)
}

dtls_option = get_option('dtls').enable_if(get_option('webrtc').enabled(), error_message: 'webrtc option is enabled')
openssl_dep = dependency('openssl', version: '>= 1.0.1', required: dtls_option)
libcrypto_dep = dependency('libcrypto', required: dtls_option)

if openssl_dep.found() and libcrypto_dep.found()
  gstdtls = library('gstdtls',
    dtls_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gst_dep, libcrypto_dep, openssl_dep] + winsock2,
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstdtls]
endif
