@echo off
rem Build the HMI gauge demo inside the self-contained HMI Docker image.
rem No workspace mount is needed: the image contains the baked workspace at
rem /workdir (see docker/rebake-workspace.sh + "make stage && make image").
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%