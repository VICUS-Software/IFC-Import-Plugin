CONFIG -= qt

TEMPLATE = lib
#CONFIG += staticlib

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

greaterThan(QT_MAJOR_VERSION, 4) {
	contains(QT_ARCH, i386): {
		DIR_PREFIX =
	} else {
		DIR_PREFIX = _x64
	}
} else {
	DIR_PREFIX =
}

# using of shared libs only for non MC compiler
# MS compiler needs explicite export statements in case of shared libs
	win32-msvc* {
		CONFIG += static
		DEFINES += NOMINMAX
		DEFINES += _CRT_SECURE_NO_WARNINGS
		CONFIG(debug, debug|release) {
			QMAKE_CXXFLAGS += /GS /RTC1
		}
	}
	else {
		CONFIG += shared_and_static build_all
	}

# disable warning for unsafe functions if using MS compiler
	win32-msvc* {
		QMAKE_CXXFLAGS += /wd4996
		QMAKE_CFLAGS += /wd4996
		QMAKE_CXXFLAGS += /std:c++17
	}
	else {
		QMAKE_CXXFLAGS += -std=c++17
	}

LIBS += -L../../../../../../../../lib$${DIR_PREFIX}

CONFIG(debug, debug|release) {
	# Build into the common lib dir (externals/lib/debug) so Carve sits alongside all
	# the other libraries and is found at runtime like every other dependency.
	# (Previously it went to externals/lib_x64, which is not on the runtime lib path.)
	DESTDIR = ../../../../../../../../lib/debug
	OBJECTS_DIR = debug$${DIR_PREFIX}
	windows {
		contains( OPTIONS, top_level_libs ) {
			DLLDESTDIR = ../../../../../../../../../bin/debug$${DIR_PREFIX}
		}
		else {
			DLLDESTDIR = ../../../../../../../../../../bin/debug$${DIR_PREFIX}
		}
	}
}
else {
	DESTDIR = ../../../../../../../../lib/release
	OBJECTS_DIR = release$${DIR_PREFIX}
	windows {
		contains( OPTIONS, top_level_libs ) {
			DLLDESTDIR = ../../../../../../../../../bin/release$${DIR_PREFIX}
		}
		else {
			DLLDESTDIR = ../../../../../../../../../../bin/release$${DIR_PREFIX}
		}
	}
}

INCLUDEPATH = \
	../../src/include \
	../../src/common

SOURCES += \
	../../src/common/geometry.cpp \
	../../src/lib/aabb.cpp \
	../../src/lib/carve.cpp \
	../../src/lib/convex_hull.cpp \
	../../src/lib/csg.cpp \
	../../src/lib/csg_collector.cpp \
	../../src/lib/edge.cpp \
	../../src/lib/face.cpp \
	../../src/lib/geom.cpp \
	../../src/lib/geom2d.cpp \
	../../src/lib/geom3d.cpp \
	../../src/lib/intersect.cpp \
	../../src/lib/intersect_classify_edge.cpp \
	../../src/lib/intersect_classify_group.cpp \
	../../src/lib/intersect_debug.cpp \
	../../src/lib/intersect_face_division.cpp \
	../../src/lib/intersect_group.cpp \
	../../src/lib/intersect_half_classify_group.cpp \
	../../src/lib/intersection.cpp \
	../../src/lib/math.cpp \
	../../src/lib/mesh.cpp \
	../../src/lib/octree.cpp \
	../../src/lib/pointset.cpp \
	../../src/lib/polyhedron.cpp \
	../../src/lib/polyline.cpp \
	../../src/lib/shewchuk_predicates.cpp \
	../../src/lib/tag.cpp \
	../../src/lib/timing.cpp \
	../../src/lib/triangle_intersection.cpp \
	../../src/lib/triangulator.cpp

HEADERS += \
	../../src/common/geometry.hpp \
	../../src/common/opts.hpp \
	../../src/common/rgb.hpp \
	../../src/common/stringfuncs.hpp \
	../../src/include/carve/aabb.hpp \
	../../src/include/carve/aabb_impl.hpp \
	../../src/include/carve/carve.hpp \
	../../src/include/carve/cbrt.h \
	../../src/include/carve/classification.hpp \
	../../src/include/carve/collection.hpp \
	../../src/include/carve/collection_types.hpp \
	../../src/include/carve/colour.hpp \
	../../src/include/carve/convex_hull.hpp \
	../../src/include/carve/csg.hpp \
	../../src/include/carve/csg_triangulator.hpp \
	../../src/include/carve/debug_hooks.hpp \
	../../src/include/carve/djset.hpp \
	../../src/include/carve/edge_decl.hpp \
	../../src/include/carve/edge_impl.hpp \
	../../src/include/carve/exact.hpp \
	../../src/include/carve/face_decl.hpp \
	../../src/include/carve/face_impl.hpp \
	../../src/include/carve/faceloop.hpp \
	../../src/include/carve/geom.hpp \
	../../src/include/carve/geom2d.hpp \
	../../src/include/carve/geom3d.hpp \
	../../src/include/carve/geom_impl.hpp \
	../../src/include/carve/heap.hpp \
	../../src/include/carve/input.hpp \
	../../src/include/carve/interpolator.hpp \
	../../src/include/carve/intersection.hpp \
	../../src/include/carve/iobj.hpp \
	../../src/include/carve/kd_node.hpp \
	../../src/include/carve/math.hpp \
	../../src/include/carve/math_constants.hpp \
	../../src/include/carve/matrix.hpp \
	../../src/include/carve/mesh.hpp \
	../../src/include/carve/mesh_impl.hpp \
	../../src/include/carve/mesh_ops.hpp \
	../../src/include/carve/mesh_simplify.hpp \
	../../src/include/carve/octree_decl.hpp \
	../../src/include/carve/octree_impl.hpp \
	../../src/include/carve/pointset.hpp \
	../../src/include/carve/pointset_decl.hpp \
	../../src/include/carve/pointset_impl.hpp \
	../../src/include/carve/pointset_iter.hpp \
	../../src/include/carve/poly.hpp \
	../../src/include/carve/poly_decl.hpp \
	../../src/include/carve/poly_impl.hpp \
	../../src/include/carve/polyhedron_base.hpp \
	../../src/include/carve/polyhedron_decl.hpp \
	../../src/include/carve/polyhedron_impl.hpp \
	../../src/include/carve/polyline.hpp \
	../../src/include/carve/polyline_decl.hpp \
	../../src/include/carve/polyline_impl.hpp \
	../../src/include/carve/polyline_iter.hpp \
	../../src/include/carve/rescale.hpp \
	../../src/include/carve/rtree.hpp \
	../../src/include/carve/shewchuk_predicates.hpp \
	../../src/include/carve/spacetree.hpp \
	../../src/include/carve/tag.hpp \
	../../src/include/carve/timing.hpp \
	../../src/include/carve/tree.hpp \
	../../src/include/carve/triangle_intersection.hpp \
	../../src/include/carve/triangulator.hpp \
	../../src/include/carve/triangulator_impl.hpp \
	../../src/include/carve/util.hpp \
	../../src/include/carve/vcpp_config.h \
	../../src/include/carve/vector.hpp \
	../../src/include/carve/vertex_decl.hpp \
	../../src/include/carve/vertex_impl.hpp \
	../../src/include/carve/win32.h \
	../../src/include/carve/xcode_config.h \
	../../src/lib/csg_collector.hpp \
	../../src/lib/csg_data.hpp \
	../../src/lib/csg_detail.hpp \
	../../src/lib/intersect_classify_common.hpp \
	../../src/lib/intersect_classify_common_impl.hpp \
	../../src/lib/intersect_common.hpp \
	../../src/lib/intersect_debug.hpp

# Default rules for deployment.
unix {
	target.path = $$[QT_INSTALL_PLUGINS]/generic
}
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
	../../src/include/carve/cmake-config.h.in \
	../../src/include/carve/config.h.in
