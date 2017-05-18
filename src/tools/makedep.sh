#! /bin/sh
echo "# This file has been automatically created using makedep.sh" > Makefile.dependencies
echo >> Makefile.dependencies
tools/makedep "" *.h >> Makefile.dependencies
echo >> Makefile.dependencies
tools/makedep "" leetnet/*.h >> Makefile.dependencies
echo >> Makefile.dependencies
tools/makedep "\$(OBJCOMPILE)" *.cpp >> Makefile.dependencies
echo >> Makefile.dependencies
tools/makedep "\$(LEETNETCOMPILE)" leetnet/*.cpp >> Makefile.dependencies
echo >> Makefile.dependencies
tools/makedep "\$(OBJCOMPILE)" tools/*.cpp >> Makefile.dependencies
