generate_certs = find_program('generate_cert.sh')
certs = custom_target(
        'generate-certs',
        command: [generate_certs, '@OUTDIR@'],
        output : ['key.pem', 'cert.pem']
)

simple_server = files('simple_server.py')
