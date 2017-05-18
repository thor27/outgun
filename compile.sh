#!/bin/bash
#
# Compile script for Outgun 1.0.0, and may work in later versions.
# by ThOR27 - Thomaz de Oliveira dos Reis - thommy@globo.com
# modified for Outgun 1.0.3 by Nix - Niko Ritari - npr1@suomi24.fi
#

# This function was taken from the wineinstall script.
# for more information about what is wine www.winehq.org
function conf_yesno_answer {
	unset ANSWER
	while [ "$ANSWER" != 'yes' ] && [ "$ANSWER" != 'no' ]
	do {
		echo -n "$1"
		read ANSWER
	}
	done
}

function downandinstall {
	name="$1"
	compile1="$2"
	compile2="$3"
	install="$4"
	siteurl="$5"
	fileurl="$6"
	filename="$7"
	subdir="$8"

	echo
	echo "Couldn't compile with $name, probably you don't have $name installed"
	echo "on your system."
	echo "You can try to download it as a package fom your distro repository or"
	echo "manually download and install from $siteurl."
	echo "If you prefer, this script will try to download $name sources, compile"
	echo "and install."
	if [ `whoami` != 'root' ]; then {
		echo "For the installation process you will need your root password."
	} fi
	echo
	echo "Do you wan't the script to try downloading and installing $name?"

	conf_yesno_answer "(yes/no) "

	if [ "$ANSWER" = 'yes' ]; then {
		if ! [ -f "$filename" ]; then {
			wget "$fileurl"
			echo
		} fi
		if [ -f "$filename" ]; then {
			rm -rf "$subdir"
			if [ "`tar -zxf "$filename" 2>&1`" = "" ]; then {
				cd $subdir
				if { $compile1 && $compile2; } then {
					if [ `whoami` = 'root' ]; then {
						if ! ( $install && /sbin/ldconfig ); then {
							echo
							echo "Problem installing $name."
							exit 1
						} fi
					}
					else {
						echo
						echo "Enter root password"
						if ! su root -c "$install && /sbin/ldconfig" && ! su root -c "$install && /sbin/ldconfig"; then {
							echo
							echo "Problem installing $name."
							exit 1
						} fi
					} fi
					echo
					echo "$name installed. Running the script again to see if everything is OK."
					echo
					cd ../
					sh ./compile.sh
				}
				else {
					echo
					echo "Couldn't compile $name. You will have to try to fix it manually."
				} fi
			}
			else {
				echo
				echo "Problems uncompressing $name. Trying again may help."
				rm -f "$filename"
			} fi
		}
		else {
			echo "Problems downloading $name, check you internet connection and try again."
		} fi
	}
	else {
		echo
		echo "OK, so install $name in your system and run this script again."
	} fi
}

function clean {
	rm -f ./allegro.check.cpp
	rm -f ./hawk.check.cpp
	rm -f ./g++.check.cpp
	rm -f ./allegro.check.bin
	rm -f ./hawk.check.bin
	rm -f ./g++.check.bin
}

echo
echo ===============================================================================
echo "This script will try to compile Outgun, compiling and installing its"
echo "dependecies if needed."
echo "It's released under the GPL. Read docs for more info."
echo "WARNING: This script is still a BETA version. Please, report any errors to the"
echo "FORUM at outgun.sf.net or by email at: thommy@globo.com"
echo ===============================================================================
echo
echo "Cleaning files from an old execution. . ."
clean

if [ ! -d "./src" ]; then {
	echo
	echo "There is no Outgun src directory. This script should be run in the"
	echo "directory where you extracted Outgun."
	exit 1
} fi

echo
echo "Creating CPP files that will be used for checking. . ."

#=====================================
# HawkNL Checker by Nix - Niko Ritari
#=====================================
cat > ./hawk.check.cpp <<EOF
#include <nl.h>

int main() {
	return nlInit() == NL_FALSE;
}

EOF
#=====================================

#=====================================
# Allegro Checker by Nix - Niko Ritari
#=====================================
cat > ./allegro.check.cpp <<EOF
#include <allegro.h>

int main() {
	(void)GFX_XWINDOWS_FULLSCREEN;
	allegro_init();
	return 0;
}

EOF
#=====================================

#=====================================
# g++ Checker by Nix - Niko Ritari
#=====================================
cat > ./g++.check.cpp <<EOF
#include <sstream>

using std::istringstream;
int main() {
	for (int i; false; );
	int i;
	return 0;
}

EOF
#=====================================

echo
echo "Checking g++"
echo "Compiling test program. . ."
g++ g++.check.cpp -o g++.check.bin
if ! [ -f "./g++.check.bin" ]; then {
	echo
	echo "A new enough g++ was not found in your system. Get it or, if you have it,"
	echo "update to a newer version."
	exit 1
} fi

echo "g++ OK!"
echo
echo "Checking HawkNL"
echo "Compiling test program. . ."

g++ hawk.check.cpp -o hawk.check.bin -lNL -pthread

if ! [ -f "./hawk.check.bin" ]; then {
	downandinstall "HawkNL" "make -f makefile.linux" "true" "make -f makefile.linux install" "http://www.hawksoft.com/hawknl/" "http://koti.mbnet.fi/outgun/dependencies/outgun_1.0_hawknl_src.tar.gz" "./outgun_1.0_hawknl_src.tar.gz" "./hawknl*"
	exit 0
} fi

echo "Running test program. . ."
if { ./hawk.check.bin; } then {
	echo "Running just fine. HawkNL is OK."
}
else {
	echo
	echo "WARNING: Problems running a program compiled with HawkNL. Outgun may compile"
	echo "         but will not run."
	if [ -f /etc/ld.so.conf -a `grep "^/usr/local/lib$" /etc/ld.so.conf | wc -l` -eq "0" ]; then {
		echo
		echo "It seems /usr/local/lib is not mentioned in your /etc/ld.so.conf. The HawkNL"
		echo "library .so is by default installed there. Try adding the line or copying"
		echo "the library to another lib directory. Then run ldconfig and try again."
	}
	else {
		echo "         (Re)install a new version of HawkNL (http://www.hawksoft.com/hawknl/)"
		echo "         and test it again, or try to resolve the error otherwise."
	} fi

	echo
	echo "Do you want to continue the script and ignore the problem?"
	conf_yesno_answer "(yes/no) "
	if [ "$ANSWER" = 'no' ]; then {
		exit 0
	} fi
} fi

echo
echo "Checking Allegro"
echo "Compiling test program. . ."

g++ allegro.check.cpp -o allegro.check.bin `allegro-config --libs`

if ! [ -f "./allegro.check.bin" ]; then {
	downandinstall "Allegro" "./configure" "make" "make install" "http://alleg.sf.net/" "http://koti.mbnet.fi/outgun/dependencies/outgun_1.0_allegro_src.tar.gz" "./outgun_1.0_allegro_src.tar.gz" "./allegro-*"
	exit 0
} fi

echo "Running test program. . ."
if [ "`./allegro.check.bin 2>&1`" == "" ]; then {
	echo "Running just fine. Allegro is OK."
}
else {
	echo
	echo "WARNING: Problems running a program compiled with Allegro. Outgun may compile"
	echo "         but will not run."
	echo "         (Re)install a newer version of Allegro (http://alleg.sf.net/) and"
	echo "         test it again, or try to resolve the error otherwise."

	echo
	echo "Do you want to continue the script and ignore the problem?"
	conf_yesno_answer "(yes/no) "
	if [ "$ANSWER" = 'no' ]; then {
		exit 0
	} fi
} fi

echo
echo "Now Outgun is going to be compiled, and if there were no errors or warnings,"
echo "you should be playing soon. Or not so soon, depending of your machine speed."
echo "In my XP2200+ with 512 DDR 400, this process takes almost 5 minutes."
echo
echo "Compiling Outgun. . ."

rm -f outgun

make -B -C src LINUX=1

if [ -f "outgun" ]; then {
	echo
	echo "Outgun compiled fine. You should run now by typing ./outgun"
	echo
	echo "Have a nice play!"
	echo
	clean
}
else {
	echo
	echo "I don't know how but Outgun didn't compile."
	echo "Check the error messages and try to find out yourself the reason and run"
	echo "this script again."
} fi
