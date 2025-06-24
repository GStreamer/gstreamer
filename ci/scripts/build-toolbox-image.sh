#!/bin/bash
# vi: sw=2 ts=4

# Shameless copy of the script from gnome-shell
# https://gitlab.gnome.org/GNOME/gnome-shell/-/blob/main/.gitlab-ci/build-toolbox-image.sh?ref_type=heads

set -e

die() {
  echo "$@" >&2
  exit 1
}

build_container() {
  local tmptag="localhost/rebuilt-tmp-tag"
  echo Rebuilding image: $BASE_CI_IMAGE

  export BUILDAH_ISOLATION=chroot
  export BUILDAH_FORMAT=docker

  local build_cntr=$(buildah from $BASE_CI_IMAGE)
  local build_mnt=$(buildah mount $build_cntr)

  [[ -n "$build_mnt" && -n "$build_cntr" ]] || die "Failed to mount the container"

  # Copy pasted from github
  # https://github.com/containers/toolbox/blob/main/images/fedora/f39/extra-packages
  local extra_packages=(
    bash-completion
    bc
    bzip2
    cracklib-dicts
    diffutils
    dnf-plugins-core
    findutils
    flatpak-spawn
    fpaste
    gawk-all-langpacks
    git
    glibc-gconv-extra
    gnupg2
    gnupg2-smime
    gvfs-client
    hostname
    iproute
    iputils
    keyutils
    krb5-libs
    less
    lsof
    man-db
    man-pages
    mesa-dri-drivers
    mesa-vulkan-drivers
    mtr
    nano-default-editor
    nss-mdns
    openssh-clients
    passwd
    pigz
    procps-ng
    psmisc
    rsync
    shadow-utils
    sudo
    tcpdump
    "time"
    traceroute
    tree
    unzip
    util-linux
    vte-profile
    vulkan-loader
    wget
    which
    whois
    words
    xorg-x11-xauth
    xz
    zip
  )
  local our_extra_packages=(
    gdb
    ripgrep
    fish
    zsh
  )
  # local debug_packages=(
  #   glib2
  # )

  buildah run $build_cntr sudo dnf install -y "${extra_packages[@]}"
  buildah run $build_cntr sudo dnf install -y "${our_extra_packages[@]}"
  # buildah run $build_cntr dnf debuginfo-install -y "${debug_packages[@]}"

  buildah run $build_cntr sudo dnf clean all
  buildah run $build_cntr sudo rm -rf /var/lib/cache/dnf

  buildah config \
    --env RUSTUP_HOME="/usr/local/rustup" \
    --env CARGO_HOME="/usr/local/cargo/" \
    --env PATH="$PATH:/usr/local/cargo/bin/" \
    $build_cntr

  # Remove the hardcoded HOME env var that ci-templates adds
  # https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/2433#note_2243222
  # Also add the OCI labels that toolbox expects, to advertize that the image is compatible
  buildah config --env HOME- \
    --label com.github.containers.toolbox=true \
    --label org.opencontainers.image.base.name=$BASE_CI_IMAGE \
    $build_cntr

  buildah commit $build_cntr $tmptag

  # Retag the image to have the same tag as the base image
  buildah tag $tmptag $BASE_CI_IMAGE

  # Unmount and remove the container
  buildah umount "$build_cntr"
  buildah rm "$build_cntr"
}

BASE_CI_IMAGE="$1"
gstbranch="${GST_UPSTREAM_BRANCH:-main}"

[[ -n "$BASE_CI_IMAGE" ]] ||
  die "Usage: $(basename $0) BASE_CI_IMAGE "

[[ -n "$CI_REGISTRY" && -n "$CI_REGISTRY_USER" && -n "$CI_REGISTRY_PASSWORD" ]] ||
  die "Insufficient information to log in."

podman login -u $CI_REGISTRY_USER -p $CI_REGISTRY_PASSWORD $CI_REGISTRY

build_container

echo "Publishing $BASE_CI_IMAGE"
podman push "$BASE_CI_IMAGE"

# Publish an unversioned ref as well that we can always fetch
if [ "$CI_COMMIT_BRANCH" == "main" ] && [ "$CI_PROJECT_NAMESAPCE" == "gstreamer" ]; then
  latest="$CI_REGISTRY_IMAGE/$FDO_REPO_SUFFIX:gst-toolbox-main"
  podman tag "$BASE_CI_IMAGE" "$latest"
  podman push "$latest"
fi

echo "Create your toolbox with either of the following commands"
echo "     $ toolbox create gst-$gstbranch --image $BASE_CI_IMAGE"
