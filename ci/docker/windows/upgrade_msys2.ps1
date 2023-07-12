$ErrorActionPreference = 'Continue'
# Perform a sysupgrade on MSYS2, otherwise it will do the upgrade at a later stage
while (1) {
  C:\msys64\msys2_shell.cmd -ucrt64 -defterm -here -no-start -use-full-path -lc 'pacman -Syuu --noconfirm'
  if (!$?) {
    Write-Host "System upgrade needed to kill MSYS2 processes or something, running it again..."
    continue
  }
  Write-Host "MSYS2 system upgrade complete"
  break
}
