# escape=`

FROM 'mcr.microsoft.com/windows/servercore:ltsc2019'

# Make sure any failure in PowerShell scripts is fatal
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop';"]
ENV ErrorActionPreference='Stop'

# Install Chocolatey
RUN iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
# Install required packages
RUN choco install -y vcredist140
RUN choco install -y cmake --installargs 'ADD_CMAKE_TO_PATH=System'
RUN choco install -y git --params '/NoAutoCrlf /NoCredentialManager /NoShellHereIntegration /NoGuiHereIntegration /NoShellIntegration'
RUN choco install -y git-lfs
RUN choco install -y 7zip
RUN choco install -y windows-sdk-8.1
RUN choco install -y python3
RUN choco install -y msys2 --params '/NoPath /NoUpdate /InstallDir:C:\\msys64'

# Visual Studio can't be installed with choco.
# It depends on dotnetfx v4.8.0.20190930, which requires a reboot: dotnetfx (exit code 3010)
# https://github.com/microsoft/vs-dockerfiles/blob/main/native-desktop/
# Set up environment to collect install errors.
COPY Install.cmd C:\TEMP\
ADD https://aka.ms/vscollect.exe C:\TEMP\collect.exe
# Download channel for fixed install.
ARG CHANNEL_URL=https://aka.ms/vs/15/release/channel
ADD ${CHANNEL_URL} C:\TEMP\VisualStudio.chman
# Download and install Build Tools for Visual Studio 2017 for native desktop workload.
ADD https://aka.ms/vs/15/release/vs_buildtools.exe C:\TEMP\vs_buildtools.exe
RUN C:\TEMP\Install.cmd C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache `
    --channelUri C:\TEMP\VisualStudio.chman `
    --installChannelUri C:\TEMP\VisualStudio.chman `
    --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended`
    --installPath C:\BuildTools

RUN pip3 install meson

RUN 'git config --global user.email "cirunner@gstreamer.freedesktop.org"; git config --global user.name "GStreamer CI system"'

COPY install_mingw.ps1 C:\
RUN C:\install_mingw.ps1

ARG DEFAULT_BRANCH="main"

COPY prepare_gst_env.ps1 C:\
RUN C:\prepare_gst_env.ps1

COPY prepare_cerbero_env.sh  C:\
RUN C:\MinGW\msys\1.0\bin\bash.exe --login -c "C:/prepare_cerbero_env.sh"