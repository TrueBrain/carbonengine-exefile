@echo off
echo Checking out project and filters file
p4 edit ExeFile.vcxproj
p4 edit ExeFile.vcxproj.filters
echo Regenerating
..\..\..\..\..\..\shared_tools\python\27\python.exe ..\..\tools\ProjectFileGenerator\ProjectFileGenerator.py -i ExeFile.ccpproj
pause
