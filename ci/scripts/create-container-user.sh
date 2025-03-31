#! /bin/bash

set -eux

uid="10043"
name="containeruser"
groupadd $name -g $uid
useradd -u $uid -g $uid -ms /bin/bash $name

usermod -aG wheel $name || usermod -aG sudo $name
bash -c "echo $name ALL=\(ALL\) NOPASSWD:ALL > /etc/sudoers.d/$name"
chmod 0440 /etc/sudoers.d/$name
