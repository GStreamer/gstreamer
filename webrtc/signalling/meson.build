simple_server = files('simple_server.py')

openssl = find_program ('openssl', required : false)
if openssl.found()
  # TODO: a better cert generation algorithm that doesn't rely on calling openssl
  generate_certs = find_program('generate_cert.sh')
  certs = custom_target(
        'generate-certs',
        command: [generate_certs, '@OUTDIR@'],
        output : ['key.pem', 'cert.pem']
  )
endif
