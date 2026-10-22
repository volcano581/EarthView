@echo off
rem Starts the tactical map demo. Extra options are passed through, e.g.
rem   Run-Demo.cmd --units 1000 --hz 30
rem   Run-Demo.cmd --mbtiles D:\Maps\pakistan.mbtiles
cd /d "%~dp0"
start "" "%~dp0cesium_proto.exe" %*
