# CMakeLists.txt file for IFC2BESTest and dependent libraries

# Require a fairly recent cmake version
cmake_minimum_required( VERSION 2.8...3.13 )

# The project name
project( IFC2BESTest )

# enable @rpath on MacOS
cmake_policy(SET CMP0042 NEW)

# -------------------------------------------------------------
# MACRO definitions
# -------------------------------------------------------------

# Macros to hide/show cached variables.
# These two macros can be used to "hide" or "show" in the
# list of cached variables various variables and/or options
# that depend on other options.
# Note that once a variable is modified, it will preserve its
# value (hidding it merely makes it internal)

MACRO(HIDE_VARIABLE var)
  IF(DEFINED ${var})
    SET(${var} "${${var}}" CACHE INTERNAL "")
  ENDIF(DEFINED ${var})
ENDMACRO(HIDE_VARIABLE)

MACRO(SHOW_VARIABLE var type doc default)
  IF(DEFINED ${var})
    SET(${var} "${${var}}" CACHE "${type}" "${doc}" FORCE)
  ELSE(DEFINED ${var})
    SET(${var} "${default}" CACHE "${type}" "${doc}")
  ENDIF(DEFINED ${var})
ENDMACRO(SHOW_VARIABLE)

# -------------------------------------------------------------
# Initial commands
# -------------------------------------------------------------

# Uncomment this to enable detailed make output
#set( CMAKE_VERBOSE_MAKEFILE ON )

# Hide some more cache variables to keep things tidy
MARK_AS_ADVANCED(CMAKE_BACKWARDS_COMPATIBILITY)
MARK_AS_ADVANCED(EXECUTABLE_OUTPUT_PATH LIBRARY_OUTPUT_PATH)

# Set default build type
if (NOT CMAKE_BUILD_TYPE)
	set( CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
		"Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif (NOT CMAKE_BUILD_TYPE)

if (MSVC)

	# add VC compiler-specific flags (NONMINMAX define, disable "unsafe" warnings, enable C++17)
	set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D\"NOMINMAX\" /wd4996 /std:c++17 /Zc:__cplusplus /permissive-" )

else (MSVC)

	# on Unix we want really detailed warnings
	ADD_DEFINITIONS( -Wall -fPIC)
	# when building with gcc/icc add compile flag
	if (APPLE)
		#message( STATUS "Mac compiler ID = ${CMAKE_C_COMPILER_ID}" )
		if ( CMAKE_C_COMPILER_ID MATCHES "GNU")
			set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17" )
		else ( CMAKE_C_COMPILER_ID MATCHES "GNU")
			set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++" )
		endif ( CMAKE_C_COMPILER_ID MATCHES "GNU")
	else (APPLE)
		set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -std=gnu++17" )
	endif (APPLE)

endif (MSVC)


# -------------------------------------------------------------
# Packages
# -------------------------------------------------------------

# automatically add CMAKE_CURRENT_SOURCE_DIR and CMAKE_CURRENT_BINARY_DIR to the include directories in every processed CMakeLists.txt
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set Qt version for QtExt compatibility (from SIM-VICUS)
set(VICUS_QT_VERSION 6 CACHE INTERNAL "Qt version in use")

find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(Qt6 COMPONENTS Gui REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(Qt6 COMPONENTS Xml REQUIRED)
find_package(Qt6 COMPONENTS Svg REQUIRED)
find_package(Qt6 COMPONENTS PrintSupport REQUIRED)
find_package(Qt6 COMPONENTS Network REQUIRED)
find_package(Qt6 COMPONENTS Concurrent REQUIRED)

# Populate Qt6 include dir variables for compatibility with libraries that use them
get_target_property(Qt6Core_INCLUDE_DIRS Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Qt6Gui_INCLUDE_DIRS Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Qt6Widgets_INCLUDE_DIRS Qt6::Widgets INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Qt6PrintSupport_INCLUDE_DIRS Qt6::PrintSupport INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Qt6Network_INCLUDE_DIRS Qt6::Network INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(Qt6Svg_INCLUDE_DIRS Qt6::Svg INTERFACE_INCLUDE_DIRECTORIES)

message("*** Building with Qt6, Version ${Qt6_VERSION} ***")

# when building with gcc/icc add compile flag
if (NOT MSVC)
	if (APPLE)
	  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++" )
	else (APPLE)
	  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -std=gnu++17" )
	endif (APPLE)
endif (NOT MSVC)

find_package(OpenMP)
if(OPENMP_FOUND)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

if( APPLE )
	find_library(IOKIT NAMES IOKit REQUIRED)
	find_library(COREFOUNDATION NAMES CoreFoundation REQUIRED)
	find_library(SECURITY_FRAMEWORK Security REQUIRED)
	find_library(SYSTEM_CONFIGURATION SystemConfiguration REQUIRED)

	set(APPLE_FRAMEWORKS
		${IOKIT}
		${SECURITY_FRAMEWORK}
		${COREFOUNDATION}
		${SYSTEM_CONFIGURATION}
	)
endif( APPLE )

# -------------------------------------------------------------
# HiGHS Linear Programming Solver (pre-built, required by Vicus)
# -------------------------------------------------------------
set(HIGHS_DIR "${CMAKE_SOURCE_DIR}/externals/HiGHS")

if (NOT EXISTS "${HIGHS_DIR}/src/Highs.h")
	message(FATAL_ERROR "HiGHS not found in externals/HiGHS/. Run externals/HiGHS/download-highs_VC19_x64.bat first.")
endif()

set(HIGHS_INCLUDE_DIRS "${HIGHS_DIR}/src")

find_library(HIGHS_LIBRARY_RELEASE NAMES highs
	PATHS "${HIGHS_DIR}/lib_x64"
	NO_DEFAULT_PATH)

find_library(HIGHS_LIBRARY_DEBUG NAMES highsd highs
	PATHS "${HIGHS_DIR}/lib_x64"
	NO_DEFAULT_PATH)

if (NOT HIGHS_LIBRARY_RELEASE)
	message(FATAL_ERROR "HiGHS library not found in externals/HiGHS/lib_x64/. Run externals/HiGHS/download-highs_VC19_x64.bat first.")
endif()

set(HIGHS_LIBRARIES
	$<$<CONFIG:Debug>:${HIGHS_LIBRARY_DEBUG}>
	$<$<NOT:$<CONFIG:Debug>>:${HIGHS_LIBRARY_RELEASE}>
)

# -------------------------------------------------------------
# Tools of the build chain
# -------------------------------------------------------------

# NandradCodeGenerator
add_subdirectory( NandradCodeGenerator NandradCodeGenerator )

# add subdirectories for all builds
add_subdirectory( externals/IBK IBK )
add_subdirectory( externals/IBKMK IBKMK )
add_subdirectory( externals/ifcplusplus ifcplusplus )
add_subdirectory( externals/Nandrad Nandrad )
add_subdirectory( externals/VicOSM VicOSM )
add_subdirectory( externals/VicIFC VicIFC )
add_subdirectory( externals/Vicus Vicus )
add_subdirectory( externals/IFCConvert IFCConvert )
add_subdirectory( externals/ifcplusplus/src/IfcPlusPlus/src/external/Carve Carve )
add_subdirectory( externals/clipper Clipper )
add_subdirectory( externals/QtExt QtExt )
add_subdirectory( externals/CCM CCM )
add_subdirectory( externals/DataIO DataIO )
add_subdirectory( externals/TiCPP TiCPP )
add_subdirectory( externals/IFCImportPlugin ImportIFCPlugin )

# Create _static aliases for libraries that only create non-suffixed targets
# (ImportIFCPlugin links against _static variants)
if(NOT TARGET IBK_static)
	add_library(IBK_static ALIAS IBK)
endif()
if(NOT TARGET IBKMK_static)
	add_library(IBKMK_static ALIAS IBKMK)
endif()
if(NOT TARGET TiCPP_static)
	add_library(TiCPP_static ALIAS TiCPP)
endif()
if(NOT TARGET QtExt_static)
	add_library(QtExt_static ALIAS QtExt)
endif()
if(NOT TARGET clipper_static)
	add_library(clipper_static ALIAS clipper)
endif()
if(NOT TARGET Carve_static)
	add_library(Carve_static ALIAS Carve)
endif()
if(NOT TARGET ifcplusplus_static)
	add_library(ifcplusplus_static ALIAS ifcplusplus)
endif()
if(NOT TARGET Nandrad_static)
	add_library(Nandrad_static ALIAS Nandrad)
endif()
if(NOT TARGET Vicus_static)
	add_library(Vicus_static ALIAS Vicus)
endif()
if(NOT TARGET IFCConvert_static)
	add_library(IFCConvert_static ALIAS IFCConvert)
endif()
if(NOT TARGET VicOSM_static)
	add_library(VicOSM_static ALIAS VicOSM)
endif()
if(NOT TARGET VicIFC_static)
	add_library(VicIFC_static ALIAS VicIFC)
endif()

# -------------------------------------------------------------
# applications
# -------------------------------------------------------------

add_subdirectory( IFC2BESTest IFC2BESTest)
