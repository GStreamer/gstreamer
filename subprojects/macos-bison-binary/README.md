## How to generate binaries and update build files

1. Download the latest bison source tarball
1. Extract, then build it with --prefix=/
1. Install into some dir using `DESTDIR`
1. Delete all files except the following subdirs: `bin` `lib` `share/bison` `share/aclocal`
1. Rename installdir to `bison-$version-macos-$arch` where `$arch` follows Meson's CPU families list:
   https://mesonbuild.com/Reference-tables.html#cpu-families
1. `tar -cvjf bison-$version-macos-$arch.tar.bz2 bison-$version-macos-$arch/`
1. Fetch sha256sum: `shasum -256 bison-$version-macos-$arch.tar.bz2`
1. Update sha256sum in `meson.build`
1. Update `project()` version in `meson.build`

That's it!
