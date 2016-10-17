#!/usr/bin/env python3

import argparse
import os
import re
import site
import subprocess


SCRIPTDIR = os.path.abspath(os.path.dirname(__file__))


def prepend_env_var(env, var, value):
    env[var] = os.pathsep + value + os.pathsep + env.get(var, "")
    env[var] = env[var].replace(os.pathsep + os.pathsep, os.pathsep).strip(os.pathsep)


def set_prompt_var(options, env):
    ps1 = env.get("PS1")
    if ps1:
        env["PS1"] = "[gst-%s] %s" % (options.gst_version, ps1)

    prompt = env.get("PROMPT")
    if prompt:
        env["PROMPT"] = "[gst-%s] %s" % (options.gst_version, prompt)


def get_subprocess_env(options):
    env = os.environ.copy()

    PATH = env.get("PATH", "")
    subprojects_path = os.path.join(options.builddir, "subprojects")
    for proj in os.listdir(subprojects_path):
        projpath = os.path.join(subprojects_path, proj)
        if not os.path.exists(projpath):
            print("Subproject %s does not exist in %s.,\n"
                  " Make sure to build everything properly "
                  "and try again." % (proj, projpath))
            exit(1)

        envvars_file = os.path.join(projpath, os.path.basename(projpath) + "-uninstalled-envvars.py")
        if os.path.exists(envvars_file):
            envvars_env = {"envvars": {}}
            with open(envvars_file) as f:
                code = compile(f.read(), envvars_file, 'exec')
                exec(code, None, envvars_env)
            for var, value in envvars_env["envvars"].items():
                if var.startswith("+"):
                    prepend_env_var(env, var, value.strip("+"))
                else:
                    env[var] = value

        toolsdir = os.path.join(projpath, "tools")
        if os.path.exists(toolsdir):
            prepend_env_var(env, "PATH", toolsdir)

        prepend_env_var(env, "GST_PLUGIN_PATH", projpath)

    env["CURRENT_GST"] = os.path.normpath(SCRIPTDIR + "/subprojects")
    env["GST_VALIDATE_SCENARIOS_PATH"] = os.path.normpath(
        "%s/subprojects/gst-devtools/validate/data/scenarios" % SCRIPTDIR)
    env["GST_VALIDATE_PLUGIN_PATH"] = os.path.normpath(
        "%s/subprojects/gst-devtools/validate/plugins" % options.builddir)
    env["GST_VALIDATE_APPS_DIR"] = os.path.normpath(
        "%s/subprojects/gst-editing-services/tests/validate" % SCRIPTDIR)
    prepend_env_var(env, "PATH", os.path.normpath(
        "%s/subprojects/gst-devtools/validate/tools" % options.builddir))
    prepend_env_var(env, "PATH", os.path.join(SCRIPTDIR, 'meson'))
    env["PATH"] += os.pathsep + PATH
    env["GST_VERSION"] = options.gst_version
    env["GST_PLUGIN_SYSTEM_PATH"] = ""
    env["GST_PLUGIN_SCANNER"] = os.path.normpath(
        "%s/subprojects/gstreamer/libs/gst/helpers/gst-plugin-scanner" % options.builddir)
    env["GST_PTP_HELPER"] = os.path.normpath(
        "%s/subprojects/gstreamer/libs/gst/helpers/gst-ptp-helper" % options.builddir)
    env["GST_REGISTRY"] = os.path.normpath(options.builddir + "/registry.dat")
    prepend_env_var(env, 'PYTHONPATH', ':'.join(site.getsitepackages()))
    env["PYTHONPATH"] = env["PYTHONPATH"] + ':' + os.path.normpath(
        options.builddir + '/subprojects/gst-python')

    filename = "meson.build"
    sharedlib_reg = re.compile(r'\.so$|\.dylib$')
    typelib_reg = re.compile(r'.*\.typelib$')
    for root, dirnames, filenames in os.walk(os.path.join(options.builddir,
                                                          'subprojects')):
        has_typelib = False
        has_shared = False
        for filename in filenames:
            if typelib_reg.search(filename) and not has_typelib:
                has_typelib = True
                prepend_env_var(env, "GI_TYPELIB_PATH",
                                os.path.join(options.builddir, root))
                if has_shared:
                    break
            elif sharedlib_reg.search(filename) and not has_shared:
                has_shared = True
                prepend_env_var(env, "LD_LIBRARY_PATH",
                                os.path.join(options.builddir, root))
                prepend_env_var(env, "DYLD_LIBRARY_PATH",
                                os.path.join(options.builddir, root))
                if has_typelib:
                    break


    set_prompt_var(options, env)

    return env


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="gstreamer-uninstalled")

    parser.add_argument("--builddir",
                        default=os.path.join(SCRIPTDIR, "build"),
                        help="The meson build directory")
    parser.add_argument("--gst-version", default="master",
                        help="The GStreamer major version")
    options, args = parser.parse_known_args()

    if not os.path.exists(options.builddir):
        print("GStreamer not built in %s\n\nBuild it and try again" %
              options.builddir)
        exit(1)

    if not args:
        if os.name is 'nt':
            args = [os.environ.get("COMSPEC", r"C:\WINDOWS\system32\cmd.exe")]
        else:
            args = [os.environ.get("SHELL", os.path.realpath("/bin/sh"))]
        if args[0] == "/bin/bash":
            args.append("--noprofile")

    try:
        exit(subprocess.call(args, env=get_subprocess_env(options)))
    except subprocess.CalledProcessError as e:
        exit(e.returncode)
