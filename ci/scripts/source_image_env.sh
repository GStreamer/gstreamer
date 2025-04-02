#! /bin/bash

image_cache="${SUBPROJECTS_CACHE_DIR:-}"
ci_image_info="/.gstreamer-ci-linux-image";

# Print useful metadata at the start of the build
if [[ -e "/etc/os-release" ]]; then
  cat /etc/os-release
fi

if [[ -e "$ci_image_info" && -n "${CI:-}" ]]; then
  if [[ -z "$image_cache" ]]; then
    echo "Running in CI but haven't defined SUBPROJECTS_CACHE_DIR"
    exit 1
  fi
fi

whoami
id -u
id -g
date && date -u
echo $SHELL
echo $PATH

# On the CI image we install the rust toolcahin under this path
# set the HOME and PATH variables and print the versions
# of what we have installed
if [[ -e "$ci_image_info" ]]; then
  export RUSTUP_HOME="/usr/local/rustup"
  export CARGO_HOME="/usr/local/cargo"
  export PATH="/usr/local/cargo/bin:$PATH"

  rustup --version
  rustc --version
  cargo --version
  cargo cinstall --version
fi

# Only copy the cache over if the variable is set, which usually only happens on CI.
if [ -n "$image_cache" ]; then
  date -R
  ci/scripts/handle-subprojects-cache.py --cache-dir "$image_cache" subprojects/
  date -R
fi
