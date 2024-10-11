project('macos-bison-binary', version : '3.8.2')

arch = host_machine.cpu_family()

message('Downloading and extracting bison for macOS @0@...'.format(arch))

checksum = {
  'x86_64': '325e78de481fa044f0f2177d4da332a1b580bda23fdaf37e7918c5fd55240254',
  'aarch64': '932f9ec3da7a9f6c9f596094f889038340a45e84dc03082cb10ffee84b5d7af0',
}

py3 = import('python3').find_python()
ret = run_command(py3, files('download-binary.py'), meson.project_version(), arch, checksum[arch],
    check: true)

meson.override_find_program('bison', find_program(meson.project_source_root() / ret.stdout() / 'bin/bison'))
