## High level description of the files in this directory.

### virtme-run.sh

A helper script that uses 'virtme' to launch a qemu virtual machine with the
host filesystem exposed inside the virtual machine.

### run-virt-test.sh

Run the given command and retrieve the command status in the given status file.
This is necessary because virtme doesn't return the exit code of the command.

### meson.build

Contains one rule for meson test cases that launches tests inside virtual
machines.
