# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.10

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Produce verbose output by default.
VERBOSE = 1

# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/XXX/Research/temp-del/ProgrammingEnclaves

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/XXX/Research/temp-del/ProgrammingEnclaves

# Include any dependencies generated for this target.
include Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/depend.make

# Include the progress variables for this target.
include Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/progress.make

# Include the compile flags for this target's objects.
include Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/flags.make

# Object files for target Prt_static
Prt_static_OBJECTS =

# External object files for target Prt_static
Prt_static_EXTERNAL_OBJECTS =

Samples/PingEnclavePongOutside/libPrt_static.so: Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/build.make
Samples/PingEnclavePongOutside/libPrt_static.so: Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/XXX/Research/temp-del/ProgrammingEnclaves/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Linking C shared library ../../libPrt_static.so"
	cd /home/XXX/Research/temp-del/ProgrammingEnclaves/Samples/PingEnclavePongOutside/Src/app && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/Prt_static.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/build: Samples/PingEnclavePongOutside/libPrt_static.so

.PHONY : Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/build

Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/requires:

.PHONY : Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/requires

Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/clean:
	cd /home/XXX/Research/temp-del/ProgrammingEnclaves/Samples/PingEnclavePongOutside/Src/app && $(CMAKE_COMMAND) -P CMakeFiles/Prt_static.dir/cmake_clean.cmake
.PHONY : Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/clean

Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/depend:
	cd /home/XXX/Research/temp-del/ProgrammingEnclaves && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/XXX/Research/temp-del/ProgrammingEnclaves /home/XXX/Research/temp-del/ProgrammingEnclaves/Samples/PingEnclavePongOutside/Src/app /home/XXX/Research/temp-del/ProgrammingEnclaves /home/XXX/Research/temp-del/ProgrammingEnclaves/Samples/PingEnclavePongOutside/Src/app /home/XXX/Research/temp-del/ProgrammingEnclaves/Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : Samples/PingEnclavePongOutside/Src/app/CMakeFiles/Prt_static.dir/depend

