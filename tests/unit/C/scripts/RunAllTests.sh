#!/bin/sh
#set -e
#
# This is the shell script wrapper for running C unit tests
#
exitstate=0

# Set here location of FogLAMP source code
# or leave it empty and set FogLAMP includes and FogLAMP libs

FOGLAMP_SRC="/home/ubuntu/source/FogLAMP" 
# NOTE: FogLAMP libraries come from FOGLAMP_SRC/cmake_build/C/lib

# If not set ...
if [ "${FOGLAMP_SRC}" = "" ]; then
	# Set path with FogLAMP includes and FogLAMP libs:
	FOGLAMP_INCLUDE_DIRS="/usr/include/foglamp"
	FOGLAMP_LIB_DIRS="/usr/lib/foglamp"
fi

# Go back to all tests path
cd ..

if [ ! -d results ] ; then
        mkdir results
fi

cmakefile=`find . -name CMakeLists.txt`
for f in $cmakefile; do	
	dir=`dirname $f`
	echo Testing $dir
	(
		cd $dir;
		rm -rf build;
		mkdir build;
		cd build;
		echo Building Tests...;
		cmake -DFOGLAMP_SRC="${FOGLAMP_SRC}" -DFOGLAMP_INCLUDE="${FOGLAMP_INCLUDE_DIRS}" -DFOGLAMP_LIB="${FOGLAMP_LIB_DIRS}" ..;
		rc=$?
		if [ $rc != 0 ]; then
			echo cmake failed for $dir;
			exit 1
		fi
		make;
		rc=$?
		if [ $rc != 0 ]; then
			echo make failed for $dir;
			exit 1
		fi
		echo Running tests...;
		./RunTests --gtest_output=xml > /tmp/results;
		rc=$?
		if [ $rc != 0 ]; then
			exit $rc
		fi
	) >/dev/null
	rc=$?
	if [ $rc != 0 ]; then
		echo Tests for $dir failed
		cat /tmp/results
		exitstate=1
	else
		echo All tests in $dir passed
	fi
	file=`echo $dir | sed -e 's#./##' -e 's#/#_#g'`
	mv $dir/build/test_detail.xml results/${file}.xml
done
exit $exitstate
