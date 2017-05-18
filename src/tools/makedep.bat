@echo off
echo # This file has been automatically created using makedep.bat > Makefile.dependencies
echo. >> Makefile.dependencies
for %%a in (*.h) do tools\makedep "" %%a >> Makefile.dependencies
echo. >> Makefile.dependencies
for %%a in (leetnet\*.h) do tools\makedep "" %%a >> Makefile.dependencies
echo. >> Makefile.dependencies
for %%a in (*.cpp) do tools\makedep "$(OBJCOMPILE)" %%a >> Makefile.dependencies
echo. >> Makefile.dependencies
for %%a in (leetnet\*.cpp) do tools\makedep "$(LEETNETCOMPILE)" %%a >> Makefile.dependencies
echo. >> Makefile.dependencies
for %%a in (tools\*.cpp) do tools\makedep "$(OBJCOMPILE)" %%a >> Makefile.dependencies
