nunit_console = find_program('nunit-console', 'nunitlite-runner', required: get_option('tests'))

if nunit_console.found()
    nunit_version = '3.10.1'
    get_nunit_res = run_command(nuget, 'get',
        '--builddir=NUnit',
        '--nuget-name=NUnit',
        '--nuget-version', nunit_version,
        '--csharp-version=net45',
        '--current-builddir', meson.current_build_dir(),
        '--builddir', meson.build_root(),
    )


    nunit_dep = dependency('mono-nunit', required: false)
    if not nunit_dep.found()
        if get_nunit_res.returncode() != 0
            message('Failed to get NUnit: ' + get_nunit_res.stderr())
        else
            foreach path: get_nunit_res.stdout().split()
                testsenv.prepend('MONO_PATH',
                    join_paths(meson.build_root(), path.strip('-r:'), '..'))
            endforeach

            nunit_dep = declare_dependency(link_args: get_nunit_res.stdout().split(),
                version: nunit_version)
        endif
    endif
    if nunit_dep.found()
        foreach test: [
                # 'PipelineTests',
                'SdpTests'
                ]
            lib = shared_library(test, test + '.cs', 'TestBase.cs',
                        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114'],
                        dependencies: [gst_sharp_dep, nunit_dep])
            test(test, nunit_console, args: [lib.full_path()], env: testsenv)
        endforeach
    endif
else
    message('Could not find nunit-console, can\'t run unitests')
endif