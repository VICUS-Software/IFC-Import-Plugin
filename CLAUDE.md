# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Project Overview

IFC-Import-Plugin is a C++ library and Qt5 plugin that converts IFC (Industry Foundation Classes) building model files into VICUS format for building energy performance simulation. It integrates with SIM-VICUS as an import plugin.

Version 1.1, requires SIM-VICUS >= 1.5.

---

## Build Commands

### Prerequisites

External libraries (IBK, IBKMK, QtExt, etc.) are symlinked from the SIM-VICUS submodule:
```bash
cd externals
./\_createLinuxSymlinks.sh   # Linux
# createWindowsSoftlinks.bat  # Windows (admin required)
```

### CMake Build (Linux)

```bash
cd build/cmake
./build.sh [omp] [cores] [debug|release|reldeb] [verbose]
# Standard build (with OpenMP enabled — required for the parallelized
# opening-matching and updateSpaces paths):
./build.sh omp release 16
```

The `omp` flag uses a separate build dir (`bb-omp-gcc`) so OpenMP and
non-OpenMP builds don't collide. OpenMP itself is required by CMake
unconditionally (`find_package(OpenMP REQUIRED)`); the flag is a marker that
also keeps the build trees separate.

Output: `bin/release/IFC2BESTest` and `bin/release/libImportIFCPlugin.so`

### Qt Creator Build

Open `IFC-Import-Plugin.pro` in Qt Creator. Requires Qt5 (Core, Widgets, Gui, Xml, Svg, PrintSupport, Network, Concurrent).

### Windows

```bat
build\cmake\build_VC_x64.bat
```

### Deploy Plugin

Copy `libImportIFCPlugin.so` (or `.dll`) to the SIM-VICUS plugin folder from `externals/lib_x64/`.

---

## Testing

No automated test framework. Manual/integration testing via the `IFC2BESTest` Qt GUI application which reads IFC files and runs the conversion pipeline. Test IFC files are in `data/tests/`.

---

## Architecture

### Conversion Pipeline

```
IFC file → ifcplusplus parser → BuildingModel → IFCReader.convert() → VICUS XML
```

**IFCReader** (`IFCC_IFCReader`) is the main entry point: `read()` → `convert()` → `writeXML()`.

### Key Components (all in `IFCC` namespace)

**Spatial hierarchy:** Site → Building → BuildingStorey → Space → Surface/SpaceBoundary

**Building elements:** BuildingElement (walls, roofs, slabs), Opening (doors, windows), Surface, SpaceBoundary

**Data objects:** Material, Construction, Component, SubSurfaceComponent, Window, WindowGlazing

**Geometry conversion chain:** GeometryConverter → RepresentationConverter → SolidModelConverter / FaceConverter / ProfileConverter / CurveConverter / PointConverter

**Utilities:** GeomUtils (geometry), MeshUtils/MeshOps (mesh processing), Clippertools (polygon clipping), CSG_Adapter (Carve CSG operations)

**Aggregators:** Database (all DB objects), Instances (component connections), BuildingElementsCollector

### Plugin Integration

`IFCImportPlugin` implements `SVImportPluginInterface` and shows `ImportIFCDialog` for user interaction.

### Library Dependency Order

```
IBK → IBKMK → TiCPP → ifcplusplus → Carve → Clipper → IFCConvert → IFCImportPlugin
```

### Key Type Definitions (`IFCC_Types.h`)

- `BuildingElementTypes`: BET_Wall, BET_Window, BET_Door, BET_Roof, BET_Slab, etc.
- `BasicType`: BT_Real, BT_Virtual, BT_Missing
- `ObjectType`: OT_BuildingElement, OT_Space, OT_SpaceBoundary, etc.
- `ConvertError`: error reporting struct with object type, ID, and message

---

## Coding Rules

Follows the SIM-VICUS coding standards (see `SIM-VICUS/CLAUDE.md` for full details).

### Formatting

- **Tabs only** (displayed as 4 spaces), never spaces
- UTF-8, Unix (LF) line endings
- Line length ≤ 120 chars

### Naming

- Files: `IFCC_CamelCaseName.h/.cpp`
- Header guards: `#ifndef IFCC_CamelCaseNameH`
- Namespace: `IFCC` — never use `using namespace`
- Members: `m_` prefix
- Getters: `value()` not `getValue()`
- Enums: prefix + CamelCase (e.g., `BET_Wall`, `BT_Real`)

### Error Handling

```cpp
FUNCID(ClassName::methodName);
throw IBK::Exception(IBK::FormatString("Error message %1").arg(val), FUNC_ID);
```

Rethrow with context:
```cpp
catch (IBK::Exception & ex) {
    throw IBK::Exception(ex, "Context info", FUNC_ID);
}
```

### Documentation

Doxygen-style. Always include physical units: `/*! Temperature in [K]. */`

All quantities stored in **base SI units** internally.
