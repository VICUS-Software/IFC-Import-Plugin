#!/bin/bash
# Builds vicus_room_status — reports VICUS::Room::roomStatus() per room of a
# .vicus file, i.e. EXACTLY the valid/warning/error classification the VICUS
# GUI shows (linked against the same libVicus). Use this as the reference
# metric for import quality; edge-counting heuristics (vicus_quality.py)
# disagree with the GUI in both directions.
#
# Prerequisites: OMP release build (build.sh omp release) so the static libs
# exist under build/cmake/bb-omp-gcc, and a Qt6 installation.
#
# Usage: ./build_vicus_room_status.sh [qt-dir]
#        ./vicus_room_status <file.vicus> [--detail]
set -e
cd "$(dirname "$0")"

QT=${1:-$HOME/Qt/6.9.3/gcc_64}
ROOT=$(cd .. && pwd)
E=$ROOT/SIM-VICUS/externals
B=$ROOT/build/cmake/bb-omp-gcc

g++ -O3 -std=c++17 -fPIC -fopenmp -w -DNDEBUG \
	vicus_room_status.cpp \
	"$E/Vicus/srcTranslations/VICUS_KeywordListQt.cpp" \
	-I"$E/Vicus/src" -I"$E/IBK/src" -I"$E/IBKMK/src" -I"$E/Nandrad/src" \
	-I"$E/TiCPP/src" -I"$E/CCM/src" -I"$E/DataIO/src" -I"$E/glm/src" \
	-I"$E/VicOSM/src" -I"$E/VicIFC/src" \
	-I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtXml" \
	-L"$B/Vicus" -L"$B/VicOSM" -L"$B/VicIFC" -L"$B/Nandrad" -L"$B/IBKMK" \
	-L"$B/DataIO" -L"$B/CCM" -L"$B/TiCPP" -L"$B/IBK" -L"$B/Clipper" \
	-L"$QT/lib" \
	-lVicus -lVicOSM -lVicIFC -lNandrad -lIBKMK -lDataIO -lCCM -lTiCPP -lIBK \
	-lclipper -lIBK -lQt6Core -lQt6Gui -lQt6Xml \
	-o vicus_room_status

echo "built: $(pwd)/vicus_room_status (run with LD_LIBRARY_PATH=$QT/lib)"
