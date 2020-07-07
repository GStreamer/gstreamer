nunit_console = find_program('nunit-console', 'nunitlite-runner', required: get_option('tests'))

if nunit_console.found()
    nunit_tests_env = environment()
    # FIXME: port this to macOS and Windows
    nunit_tests_env.prepend('LD_LIBRARY_PATH', testsenv_ld_library_path)

    nunit_version = '3.10.1'
    get_nunit_res = run_command(nuget, 'get',
        '--builddir=NUnit',
        '--nuget-name=NUnit',
        '--nuget-version', nunit_version,
        '--csharp-version=net45',
        '--current-builddir', meson.current_build_dir(),
        '--builddir', meson.build_root(), # FIXME: --builddir specified twice?!
        check: true,
    )


    nunit_mono_path = []
    nunit_dep = dependency('mono-nunit', required: false, version: ['>=2.6', '< 2.7'])
    if not nunit_dep.found()
        foreach path: get_nunit_res.stdout().split()
          nunit_mono_path += [meson.build_root() / path.strip('-r:') / '..']
        endforeach

        nunit_dep = declare_dependency(link_args: get_nunit_res.stdout().split(),
            version: nunit_version)
    endif

    if nunit_mono_path.length() > 0
        nunit_tests_env.prepend('MONO_PATH', nunit_mono_path + mono_path)
    else
        nunit_tests_env.prepend('MONO_PATH', mono_path)
    endif

    if nunit_dep.found()
        foreach test: [
                # 'PipelineTests',
                'SdpTests',
                'AppTests'
                ]
            lib = shared_library(test, test + '.cs', 'TestBase.cs',
                        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114'],
                        dependencies: [gst_sharp_dep, nunit_dep])
            test(test, nunit_console, args: [lib.full_path()], env: nunit_tests_env)
        endforeach
    endif
else
    message('Could not find nunit-console, can\'t run unitests')
endif
