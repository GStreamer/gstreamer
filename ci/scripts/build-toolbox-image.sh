#!/bin/bash
# vi: sw=2 ts=4

# Shameless copy of the script from gnome-shell
# https://gitlab.gnome.org/GNOME/gnome-shell/-/blob/main/.gitlab-ci/build-toolbox-image.sh?ref_type=heads

set -e

die() {
  echo "$@" >&2
  exit 1
}

check_image_base() {
  local base=$(
    skopeo inspect docker://$TOOLBOX_IMAGE 2>/dev/null |
    jq -r '.Labels["org.opencontainers.image.base.name"]')
  [[ "$base" == "$BASE_CI_IMAGE" ]]
}

build_container() {
  echo Building $TOOLBOX_IMAGE from $BASE_CI_IMAGE

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

  buildah run $build_cntr dnf -y swap coreutils-single coreutils-full
  buildah run $build_cntr dnf -y swap glibc-minimal-langpack glibc-all-langpacks

  buildah run $build_cntr dnf install -y "${extra_packages[@]}"
  buildah run $build_cntr dnf install -y "${our_extra_packages[@]}"
  # buildah run $build_cntr dnf debuginfo-install -y "${debug_packages[@]}"

  buildah run $build_cntr dnf clean all
  buildah run $build_cntr rm -rf /var/lib/cache/dnf

  # Remove the hardcoded HOME env var that ci-templates adds
  # https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/2433#note_2243222
  # Also add the OCI labels that toolbox expects, to advertize that image is compatible
  buildah config --env HOME- \
    --label com.github.containers.toolbox=true \
    --label org.opencontainers.image.base.name=$BASE_CI_IMAGE \
    $build_cntr

  buildah commit $build_cntr $TOOLBOX_IMAGE
  buildah tag $TOOLBOX_IMAGE $TOOLBOX_LATEST
}

BASE_CI_IMAGE="$1"
TOOLBOX_BRANCH="$2"
GST_UPSTREAM_BRANCH="$3"

TOOLBOX_IMAGE="$CI_REGISTRY_IMAGE/$FDO_REPO_SUFFIX:gst-toolbox-${TOOLBOX_BRANCH}"
# push an unversioned tag to make it easier to use.
# ex. pull foobar:toolbox-main
TOOLBOX_LATEST="$CI_REGISTRY_IMAGE/$FDO_REPO_SUFFIX:gst-toolbox-${GST_UPSTREAM_BRANCH}"

[[ -n "$BASE_CI_IMAGE" && -n "$TOOLBOX_BRANCH" && -n "$GST_UPSTREAM_BRANCH" ]] ||
  die "Usage: $(basename $0) BASE_CI_IMAGE TOOLBOX TAG GST_UPSTREAM_BRANCH"

[[ -n "$CI_REGISTRY" && -n "$CI_REGISTRY_USER" && -n "$CI_REGISTRY_PASSWORD" ]] ||
  die "Insufficient information to log in."

podman login -u $CI_REGISTRY_USER -p $CI_REGISTRY_PASSWORD $CI_REGISTRY

if check_image_base; then
  echo Image $TOOLBOX_IMAGE exists and is up to date.
  exit 0
fi

build_container

podman push "$TOOLBOX_IMAGE"
podman push "$TOOLBOX_LATEST"

echo "Create your toolbox with either of the following commands"
echo "     $ toolbox create gst-toolbox --image $TOOLBOX_LATEST"
echo "     $ toolbox create gst-toolbox-$TOOLBOX_BRANCH --image $TOOLBOX_IMAGE"
