# escape=`

FROM 'mcr.microsoft.com/windows/servercore:ltsc2019'

# Make sure any failure in PowerShell scripts is fatal
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop';"]
ENV ErrorActionPreference='Stop'

ARG DEFAULT_BRANCH="master"

COPY install_choco.ps1 C:\
RUN C:\install_choco.ps1

COPY install_toolchain.ps1 C:\
RUN C:\install_toolchain.ps1

COPY prepare_gst_env.ps1 C:\
RUN C:\prepare_gst_env.ps1
