************************************
C/C++ Notification server Unit Tests
************************************

This directory tree contains the unit tests for the C and C++ Notification server code.

Prequisite
==========

These tests are written using the Google Test framework. This should be installed on your machine

- sudo apt-get install libgtest-dev

Unfortunately this does not install the libraries and a manual build set is required

- cd /usr/src/gtest
- sudo cmake -E make_directory build
- sudo cmake -E chdir build cmake ..
- sudo cmake --build build
- sudo cp build/libgtest* /usr/lib

Running Tests
=============

To run all the unit tests go to the directory scripts and execute the script

- RunAllTests

This will run all unit tests and place the JUnit XML files in the directory results

NOTE:
the build process for C++ tests checks first the environment FOGLAMP_ROOT (pointing where FogLAMP has been built)
If the variable is not set then the build process uses FOGLAMP_INCLUDE_DIRS and FOGLAMP_LIB_DIRS, assuming the FogLAMP dev package has been installed.

It's possible to override defaults by changing in RunAllTests.sh script the values of FOGLAMP_SRC or FOGLAMP_INCLUDE_DIRS and FOGLAMP_LIB_DIRS before executing it.
