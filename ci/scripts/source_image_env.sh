#! /bin/bash

image_cache="${SUBPROJECTS_CACHE_DIR:-}"

# On the CI image we install the rust toolcahin under this path
# If it exists set the HOME and PATH variables and print the versions
# of what we have installed
cargo_binary="/usr/local/cargo/bin/cargo";
if [[ -e "$cargo_binary" ]]; then
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
