#! /bin/bash

builddir="$1"
tests="$2"

if [[ -z "$builddir" || -z "$tests" ]]; then
  echo "Usage: test.sh <build_directory> <test_name>"
  exit 1
fi

set -eux

_jobs=$(nproc || sysctl -n hw.ncpu)
jobs="${FDO_CI_CONCURRENT:-$_jobs}"

timeout="${TIMEOUT_FACTOR:="2"}"
validate="${EXTRA_VALIDATE_ARGS:=""}"
parent="${CI_PROJECT_DIR:-$(pwd)}"

export XDG_RUNTIME_DIR="$(mktemp -p $(pwd) -d xdg-runtime-XXXXXX)"
echo "-> Running $tests"

# Disable all cpu extensions post AVX to match what valgrind supports
# https://github.com/openssl/openssl/blob/master/NOTES-VALGRIND.md
export OPENSSL_ia32cap=":0"

# Force Software rendering for GL and Vulkan so the tests run locally
# like they would do in the CI.
export LIBGL_ALWAYS_SOFTWARE="true"
# This the hardcoded value for llvmpipe
export MESA_VK_DEVICE_SELECT="10005:0"

./gst-env.py \
    "--builddir=$builddir" \
    gst-validate-launcher "$tests" \
    --jobs "$jobs" \
    --check-bugs \
    --dump-on-failure \
    --mute \
    --shuffle \
    --no-display \
    --validate-generate-expectations=disabled \
    --meson-no-rebuild \
    --timeout-factor "$timeout" \
    --fail-on-testlist-change \
    -l "$parent/validate-logs/" \
    --xunit-file "$parent/validate-logs/xunit.xml" \
    $validate
