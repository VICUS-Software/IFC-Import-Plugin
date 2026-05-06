#!/bin/bash


# Build script for building application and all dependend libraries

# Command line options:
#   [reldeb|release|debug]		build type
#   [2 [1..n]]					cpu count
#   [verbose]					enable cmake to call verbose makefiles
#   [omp]						mark this as the OpenMP build (uses bb-omp-gcc as build dir)
#   []

# path export for mac
export PATH=~/Qt/5.11.3/gcc_64/bin:~/Qt/5.11.3/clang_64/bin:$PATH

# for MacOS, brew install of Qt 5 ("brew install qt5")
export CMAKE_PREFIX_PATH=/usr/local/opt/qt5/

CMAKELISTSDIR=$(pwd)/../..
BUILDDIR="bb"

# set defaults
CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
MAKE_CPUCOUNT="8"
BUILD_DIR_SUFFIX="gcc"
COMPILER=""
SKIP_TESTS="false"
DISABLE_GUI=0

# parse parameters, except gprof and threadchecker
for var in "$@"
do

    if [[ $var = *[[:digit:]]* ]];
    then
		MAKE_CPUCOUNT=$var
		echo "Using $MAKE_CPUCOUNT CPUs for compilation"
    fi

    if [[ $var = "debug"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=Debug"
		echo "Debug build..."
    fi

    if [[ $var = "release"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=Release"
		echo "Release build..."
    fi

    if [[ $var = "reldeb"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
		echo "RelWithDebInfo build..."
    fi

    if [[ $var = "gcc"  && $COMPILER = "" ]];
    then
		COMPILER="gcc"
		BUILD_DIR_SUFFIX="gcc"
		echo "GCC compiler build..."
		CMAKE_COMPILER_OPTIONS=""
	  fi

    if [[ $var = "verbose"  ]];
  	then
		CMAKE_OPTIONS="-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON"
	  fi

    if [[ $var = "omp"  ]];
    then
		BUILD_DIR_SUFFIX="omp-$BUILD_DIR_SUFFIX"
		echo "OpenMP build (separate build dir: bb-$BUILD_DIR_SUFFIX)..."
	  fi

done

# create build dir if not exists
BUILDDIR=$BUILDDIR-$BUILD_DIR_SUFFIX
if [ ! -d $BUILDDIR ]; then
    mkdir -p $BUILDDIR
fi

cd $BUILDDIR && cmake $CMAKE_OPTIONS $CMAKE_BUILD_TYPE $CMAKE_COMPILER_OPTIONS $CMAKELISTSDIR && make -j$MAKE_CPUCOUNT &&
cd $CMAKELISTSDIR &&
mkdir -p bin/release &&
echo "*** Copying IFC2BESTest to bin/release ***" &&
if [ -d build/cmake/$BUILDDIR/IFC2BESTest/IFC2BESTest.app ]
then
	# MacOS
	rm -rf bin/release/IFC2BESTest.app
	cp -r build/cmake/$BUILDDIR/IFC2BESTest/IFC2BESTest.app bin/release/IFC2BESTest.app &&
    echo "All files copied successfully."
else
	if [ -e build/cmake/$BUILDDIR/IFC2BESTest/IFC2BESTest ]
	then
		cp build/cmake/$BUILDDIR/IFC2BESTest/IFC2BESTest bin/release/IFC2BESTest
		cp build/cmake/$BUILDDIR/ImportIFCPlugin/libImportIFCPlugin.so bin/release/libImportIFCPlugin.so
	fi
fi

