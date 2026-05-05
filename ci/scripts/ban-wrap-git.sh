#!/bin/bash

# Exclude existing wrap-git wraps that are too difficult to move to tarballs
rm subprojects/gtk-sharp.wrap subprojects/libvmaf.wrap

wrap_gits=$(grep '^url\s*=' subprojects/*.wrap | grep -v 'https://gitlab.freedesktop.org' | wc -l)

# Ensure no new wrap-git wraps have been added, except those that we host ourselves
if ! [[ 0 -eq $wrap_gits ]]; then 
    echo "New wrap-git wraps are not allowed!"
    exit 1
fi
