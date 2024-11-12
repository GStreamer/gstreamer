# escape=`

FROM 'mcr.microsoft.com/windows/server:ltsc2022'

# Make sure any failure in PowerShell is fatal
ENV ErrorActionPreference='Stop'
# Speed up Invoke-WebRequest by disabling progress bar
ENV ProgressPreference='SilentlyContinue'
SHELL ["powershell","-NoLogo", "-NonInteractive", "-Command"]

# Not needed right now, disable for now
#RUN Install-WindowsFeature -Name Server-Media-Foundation

# Visual Studio can't be installed with choco.
# It depends on dotnetfx v4.8.0.20190930, which requires a reboot: dotnetfx (exit code 3010)
# https://github.com/microsoft/vs-dockerfiles/blob/main/native-desktop/
# Set up environment to collect install errors.
COPY Install.cmd C:\TEMP\
RUN Invoke-WebRequest -Uri https://aka.ms/vscollect.exe -OutFile C:\TEMP\collect.exe
# Download channel for fixed install.
RUN Invoke-WebRequest -Uri https://aka.ms/vs/17/release/channel -OutFile C:\TEMP\VisualStudio.chman
# Download and install Build Tools for Visual Studio 2022 for native desktop workload.
RUN Invoke-WebRequest -Uri https://aka.ms/vs/17/release/vs_buildtools.exe -OutFile C:\TEMP\vs_buildtools.exe
RUN C:\TEMP\Install.cmd C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache `
    --channelUri C:\TEMP\VisualStudio.chman `
    --installChannelUri C:\TEMP\VisualStudio.chman `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
    --includeRecommended `
    --installPath C:\BuildTools

RUN Get-ChildItem 'C:\BuildTools\VC\Tools\MSVC'
RUN Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\lib'

# Install Chocolatey
RUN iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
# Install required packages
RUN choco install -y cmake --installargs 'ADD_CMAKE_TO_PATH=System'
RUN choco install -y git --params '/NoAutoCrlf /NoCredentialManager /NoShellHereIntegration /NoGuiHereIntegration /NoShellIntegration'
RUN choco install -y git-lfs
RUN choco install -y 7zip
# See https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/427
RUN choco install -y python3 --version=3.11.2 --installargs 'AssociateFiles=1 PrependPath=1 Include_pip=1 Include_dev=1'
RUN choco install -y msys2 --params '/NoPath /NoUpdate /InstallDir:C:\\msys64'

# Install WiX v3
RUN choco install --execution-timeout=3600 -y wixtoolset

# Install Wix v5 (complete install, no NuGet) from MSI
RUN Invoke-WebRequest -Uri https://download.visualstudio.microsoft.com/download/pr/78caa28b-2982-43ed-8b9c-20e3369f0795/c771e9fd12a67068436115cf295740f7/dotnet-runtime-6.0.16-win-x86.exe -OutFile C:\TEMP\dotnet-runtime-6.0.16-win-x86.exe
RUN C:\TEMP\dotnet-runtime-6.0.16-win-x86.exe /quiet
RUN Invoke-WebRequest -Uri https://download.visualstudio.microsoft.com/download/pr/456fdf02-f100-4664-916d-fd46c192efea/619bbd8426537632b7598b4c7c467cf1/dotnet-runtime-6.0.16-win-x64.exe -OutFile C:\TEMP\dotnet-runtime-6.0.16-win-x64.exe
RUN C:\TEMP\dotnet-runtime-6.0.16-win-x64.exe /quiet
RUN Invoke-WebRequest -Uri https://github.com/wixtoolset/wix/releases/download/v5.0.1/wix-cli-x64.msi -OutFile C:\TEMP\wix-cli-x64.msi
RUN msiexec /i C:\TEMP\wix-cli-x64.msi

# Remove MAX_PATH limit of 260 characters
RUN New-ItemProperty -Path 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\FileSystem' `
    -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
RUN git config --system core.longpaths true

COPY upgrade_msys2.ps1 C:\
RUN C:\upgrade_msys2.ps1

RUN C:\msys64\msys2_shell.cmd -ucrt64 -defterm -here -no-start -use-full-path -lc 'pacman -S --noconfirm mingw-w64-ucrt-x86_64-toolchain ninja openssh'

RUN python -m pip install meson==1.5.2 python-gitlab

RUN 'git config --global user.email "cirunner@gstreamer.freedesktop.org"; git config --global user.name "GStreamer CI system"'

COPY install_rust.ps1 C:\
RUN C:\install_rust.ps1

ARG DEFAULT_BRANCH="main"
ARG CI_PROJECT_NAMESPACE
ARG CI_COMMIT_REF_NAME

ENV CI_PROJECT_NAMESPACE=$CI_PROJECT_NAMESPACE
ENV CI_COMMIT_REF_NAME=$CI_COMMIT_REF_NAME
COPY prepare_gst_env.ps1 C:\
RUN C:\prepare_gst_env.ps1

ENV CI="true"
COPY get_cerbero_clone_args.py C:\
COPY prepare_cerbero_env.sh  C:\
RUN C:\msys64\msys2_shell.cmd -ucrt64 -defterm -here -no-start -use-full-path -lc "C:/prepare_cerbero_env.sh"
