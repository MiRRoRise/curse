# CMAKE generated file: DO NOT EDIT!
# Generated by "MinGW Makefiles" Generator, CMake Version 4.0

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

SHELL = cmd.exe

# The CMake executable.
CMAKE_COMMAND = D:\msys64\mingw64\bin\cmake.exe

# The command to remove a file.
RM = D:\msys64\mingw64\bin\cmake.exe -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = C:\CourseWork2\server2

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = C:\CourseWork2\server2\build

# Include any dependencies generated for this target.
include CMakeFiles/rtp_server.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/rtp_server.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/rtp_server.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/rtp_server.dir/flags.make

CMakeFiles/rtp_server.dir/codegen:
.PHONY : CMakeFiles/rtp_server.dir/codegen

CMakeFiles/rtp_server.dir/server.cpp.obj: CMakeFiles/rtp_server.dir/flags.make
CMakeFiles/rtp_server.dir/server.cpp.obj: C:/CourseWork2/server2/server.cpp
CMakeFiles/rtp_server.dir/server.cpp.obj: CMakeFiles/rtp_server.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=C:\CourseWork2\server2\build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/rtp_server.dir/server.cpp.obj"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/rtp_server.dir/server.cpp.obj -MF CMakeFiles\rtp_server.dir\server.cpp.obj.d -o CMakeFiles\rtp_server.dir\server.cpp.obj -c C:\CourseWork2\server2\server.cpp

CMakeFiles/rtp_server.dir/server.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/rtp_server.dir/server.cpp.i"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E C:\CourseWork2\server2\server.cpp > CMakeFiles\rtp_server.dir\server.cpp.i

CMakeFiles/rtp_server.dir/server.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/rtp_server.dir/server.cpp.s"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S C:\CourseWork2\server2\server.cpp -o CMakeFiles\rtp_server.dir\server.cpp.s

CMakeFiles/rtp_server.dir/main.cpp.obj: CMakeFiles/rtp_server.dir/flags.make
CMakeFiles/rtp_server.dir/main.cpp.obj: C:/CourseWork2/server2/main.cpp
CMakeFiles/rtp_server.dir/main.cpp.obj: CMakeFiles/rtp_server.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=C:\CourseWork2\server2\build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object CMakeFiles/rtp_server.dir/main.cpp.obj"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/rtp_server.dir/main.cpp.obj -MF CMakeFiles\rtp_server.dir\main.cpp.obj.d -o CMakeFiles\rtp_server.dir\main.cpp.obj -c C:\CourseWork2\server2\main.cpp

CMakeFiles/rtp_server.dir/main.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/rtp_server.dir/main.cpp.i"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E C:\CourseWork2\server2\main.cpp > CMakeFiles\rtp_server.dir\main.cpp.i

CMakeFiles/rtp_server.dir/main.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/rtp_server.dir/main.cpp.s"
	D:\msys64\mingw64\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S C:\CourseWork2\server2\main.cpp -o CMakeFiles\rtp_server.dir\main.cpp.s

# Object files for target rtp_server
rtp_server_OBJECTS = \
"CMakeFiles/rtp_server.dir/server.cpp.obj" \
"CMakeFiles/rtp_server.dir/main.cpp.obj"

# External object files for target rtp_server
rtp_server_EXTERNAL_OBJECTS =

rtp_server.exe: CMakeFiles/rtp_server.dir/server.cpp.obj
rtp_server.exe: CMakeFiles/rtp_server.dir/main.cpp.obj
rtp_server.exe: CMakeFiles/rtp_server.dir/build.make
rtp_server.exe: CMakeFiles/rtp_server.dir/linkLibs.rsp
rtp_server.exe: CMakeFiles/rtp_server.dir/objects1.rsp
rtp_server.exe: CMakeFiles/rtp_server.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=C:\CourseWork2\server2\build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking CXX executable rtp_server.exe"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles\rtp_server.dir\link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/rtp_server.dir/build: rtp_server.exe
.PHONY : CMakeFiles/rtp_server.dir/build

CMakeFiles/rtp_server.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles\rtp_server.dir\cmake_clean.cmake
.PHONY : CMakeFiles/rtp_server.dir/clean

CMakeFiles/rtp_server.dir/depend:
	$(CMAKE_COMMAND) -E cmake_depends "MinGW Makefiles" C:\CourseWork2\server2 C:\CourseWork2\server2 C:\CourseWork2\server2\build C:\CourseWork2\server2\build C:\CourseWork2\server2\build\CMakeFiles\rtp_server.dir\DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/rtp_server.dir/depend

