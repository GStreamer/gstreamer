#!/usr/bin/env python3
#
# Called after test.sh fails on the dedicated media_check runner.
# Regenerates .media_info files and produces a diff if they changed.
# When a diff is found, writes a JUnit XML report so GitLab surfaces
# the instructions directly on the MR page (no API token needed).

import os
import subprocess
import sys
import tempfile
from xml.etree import ElementTree as ET

TESTSUITES_DIR = "subprojects/gst-integration-testsuites"


def run(cmd, **kwargs):
    return subprocess.run(cmd, **kwargs)


def regenerate_media_info(builddir):
    parent = os.environ.get("CI_PROJECT_DIR", os.getcwd())
    xdg_dir = tempfile.mkdtemp(prefix="xdg-runtime-", dir=parent)
    env = os.environ.copy()
    env["XDG_RUNTIME_DIR"] = xdg_dir
    env["LIBGL_ALWAYS_SOFTWARE"] = "true"
    env["VK_ICD_FILENAMES"] = "/usr/share/vulkan/icd.d/lvp_icd.json"

    run(
        [
            "./gst-env.py",
            f"--builddir={builddir}",
            "gst-validate-launcher",
            "validate",
            "--meson-no-rebuild",
            "--sync",
            "--update-media-info",
            "-l",
            os.path.join(parent, "validate-logs", "update"),
        ],
        env=env,
    )


def get_diff():
    result = run(
        ["git", "-C", TESTSUITES_DIR, "diff", "--quiet", "--", "media_info/"],
    )
    if result.returncode == 0:
        return None

    os.makedirs("diffs", exist_ok=True)
    result = run(
        ["git", "-C", TESTSUITES_DIR, "diff", "--", "media_info/"],
        capture_output=True,
    )
    diff = result.stdout
    with open("diffs/media_info.diff", "wb") as f:
        f.write(diff)
    return diff


def write_junit_report(message):
    parent = os.environ.get("CI_PROJECT_DIR", os.getcwd())
    report_dir = os.path.join(parent, "validate-logs")
    os.makedirs(report_dir, exist_ok=True)

    testsuite = ET.Element("testsuite", name="media_info_check",
                           tests="1", failures="1")
    testcase = ET.SubElement(testsuite, "testcase",
                             name="media_info_files_up_to_date",
                             classname="validate.media_check")
    failure = ET.SubElement(testcase, "failure",
                            message=".media_info files differ from expected")
    failure.text = message

    path = os.path.join(report_dir, "media_info_check.xml")
    ET.ElementTree(testsuite).write(path, xml_declaration=True, encoding="utf-8")
    print(f"Wrote JUnit report to {path}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <builddir>", file=sys.stderr)
        sys.exit(1)

    builddir = sys.argv[1]

    regenerate_media_info(builddir)
    diff = get_diff()

    if diff is None:
        print()
        print("No .media_info changes detected — this is a genuine test failure.")
        print()
        return

    artifacts_url = os.environ.get("CI_ARTIFACTS_URL", "")
    diff_url = f"{artifacts_url}diffs/media_info.diff"

    print(f"""
=============================================
  .media_info files differ from expected!

  This may be legitimate (e.g. a new codec/format) or it
  may indicate a regression. Please review the diff carefully
  before applying.

  Download and apply with:
    $ curl -L {diff_url} | git apply -

  (artifacts may take a few minutes to become available)
=============================================
""")

    write_junit_report(
        ".media_info files differ from expected!\n\n"
        "This may be legitimate (e.g. a new codec/format) or it may indicate "
        "a regression. Please review the diff carefully before applying.\n\n"
        "Download and apply with:\n"
        f"  curl -L {diff_url} | git apply -\n\n"
        "(artifacts may take a few minutes to become available)"
    )


if __name__ == "__main__":
    main()
