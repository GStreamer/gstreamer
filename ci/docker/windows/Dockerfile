# escape=`

FROM 'mcr.microsoft.com/windows/servercore:ltsc2019'

# Make sure any failure in PowerShell scripts is fatal
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop';"]
ENV ErrorActionPreference='Stop'

COPY install_choco.ps1 C:\
RUN C:\install_choco.ps1

RUN 'git config --global user.email "cirunner@gstreamer.freedesktop.org"; git config --global user.name "GStreamer CI system"'

COPY install_toolchain.ps1 C:\
RUN C:\install_toolchain.ps1

ARG DEFAULT_BRANCH="main"

COPY prepare_gst_env.ps1 C:\
RUN C:\prepare_gst_env.ps1

COPY prepare_cerbero_env.sh  C:\
RUN C:\MinGW\msys\1.0\bin\bash.exe --login -c "C:/prepare_cerbero_env.sh"
