# CMakeLists.txt file for IFCConvert library

project( IFCConvert )

# add include directories
include_directories(
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/ifcpp/IFC4X3/include
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/ifcpp/reader
	${PROJECT_SOURCE_DIR}/../clipper/src
	${PROJECT_SOURCE_DIR}/../IBK/src
	${PROJECT_SOURCE_DIR}/../IBKMK/src
	${PROJECT_SOURCE_DIR}/../TiCPP/src
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external/Carve/src/include
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external/glm
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external/manifold/src/utilities/include
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external/Carve/src/common
	${PROJECT_SOURCE_DIR}/../Vicus/src
	${PROJECT_SOURCE_DIR}/../Nandrad/src
	${PROJECT_SOURCE_DIR}/../CCM/src
	${PROJECT_SOURCE_DIR}/../DataIO/src
	${PROJECT_SOURCE_DIR}/../VicOSM/src
	${PROJECT_SOURCE_DIR}/../VicIFC/src
	${PROJECT_SOURCE_DIR}/../ifcplusplus/src/IfcPlusPlus/src/external/glm/glm
)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(OpenMP REQUIRED)

# collect a list of all source files of the IFCConvert library
file( GLOB LIB_SRCS
  ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# collect a list of all header files (to be used in MOC compiler)
file( GLOB LIB_HDRS ${PROJECT_SOURCE_DIR}/src/*.h )

# and finally this will run moc:
qt6_wrap_cpp( LIB_MOC_SRCS ${LIB_HDRS} )

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")
endif()

add_definitions(-DIFCQUERY_AS_DYNAMIC_LIBRARY)
add_definitions(-DIFCQUERY_LIB)

# build the library
if (NOT MSVC)
	add_library( ${PROJECT_NAME} SHARED ${LIB_SRCS} ${LIB_MOC_SRCS} )
	add_library( ${PROJECT_NAME}_static STATIC ${LIB_SRCS} ${LIB_MOC_SRCS} )
else()
	add_library( ${PROJECT_NAME} STATIC ${LIB_SRCS} ${LIB_MOC_SRCS} )
endif()

 target_link_libraries(IFCConvert PRIVATE Qt6::Core Qt6::Widgets Vicus VicIFC Nandrad OpenMP::OpenMP_CXX)
 if(TARGET IFCConvert_static)
	target_link_libraries(IFCConvert_static PRIVATE Qt6::Core Qt6::Widgets Vicus VicIFC Nandrad OpenMP::OpenMP_CXX)
 endif()
