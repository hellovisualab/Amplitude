@echo off
rem Drag a song (mp3, wav, flac...) or a folder of stems onto this file to add it to Amplitude.
rem The first time it installs what it needs (numpy, ffmpeg and, for finished songs, Demucs + PyTorch).
setlocal
cd /d "%~dp0.."
if "%~1"=="" (
  echo Arrastra una cancion ^(mp3, wav, flac...^) o una carpeta con stems encima de este archivo.
  echo Drag a song ^(mp3, wav, flac...^) or a folder of stems onto this file.
  pause
  exit /b 1
)
set "PY=python"
where py >nul 2>nul && set "PY=py -3"
%PY% -c "import numpy, imageio_ffmpeg" >nul 2>nul || %PY% -m pip install numpy imageio-ffmpeg
if exist "%~1\*" (
  %PY% Tools\import_song.py --stems "%~1"
) else (
  %PY% -c "import demucs" >nul 2>nul || %PY% -m pip install demucs
  %PY% Tools\import_song.py "%~1"
)
pause
