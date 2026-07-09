// Reads a .vicus file and reports VICUS::Room::roomStatus per room.
// Usage: vicus_openness <file.vicus> [--detail]
#include <cstdio>
#include <cstring>
#include <string>

#include <IBK_Path.h>
#include <IBK_Version.h>
#include <IBK_Exception.h>

#include <VICUS_Project.h>
#include <VICUS_Room.h>

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.vicus> [--detail]\n", argv[0]);
		return 1;
	}
	bool detail = (argc > 2 && std::strcmp(argv[2], "--detail") == 0);
	try {
		VICUS::Project prj;
		IBK::Version version;
		bool haveDrawing = false;
		prj.readXML(IBK::Path(argv[1]), version, haveDrawing);
		prj.updatePointers();

		int nValid = 0, nWarn = 0, nErr = 0, nTotal = 0;
		for (const VICUS::Building & b : prj.m_buildings) {
			for (const VICUS::BuildingLevel & bl : b.m_buildingLevels) {
				for (const VICUS::Room & r : bl.m_rooms) {
					++nTotal;
					// Diagnose surfaces whose Polygon3D has an empty polyline —
					// these throw "Polyline does not contain any vertexes!" in the GUI.
					for (const VICUS::Surface & s : r.surfaces()) {
						try {
							const auto & vv = s.geometry().polygon3D().vertexes();
							if (vv.empty())
								printf("EMPTY-POLY room='%s' surf id=%u name='%s'\n",
									   r.m_displayName.toStdString().c_str(), s.m_id,
									   s.m_displayName.toStdString().c_str());
						}
						catch (IBK::Exception &) {
							printf("THROW-POLY room='%s' surf id=%u name='%s' verts2D=%zu\n",
								   r.m_displayName.toStdString().c_str(), s.m_id,
								   s.m_displayName.toStdString().c_str(),
								   s.geometry().polygon3D().polyline().vertexes().size());
						}
					}
					VICUS::Room::RoomStatus st = r.roomStatus();
					const char* sts = "valid";
					if (st == VICUS::Room::RS_Valid) ++nValid;
					else if (st == VICUS::Room::RS_Warning) { ++nWarn; sts = "WARNING"; }
					else { ++nErr; sts = "ERROR"; }
					if (detail && st != VICUS::Room::RS_Valid) {
						double uncovLen = 0.0;
						for (const auto& seg : r.uncoveredSegments()) {
							uncovLen += (seg.m_end - seg.m_start).magnitude();
							printf("    seg (%.4f,%.4f,%.4f)->(%.4f,%.4f,%.4f) len=%.3f\n",
								   seg.m_start.m_x, seg.m_start.m_y, seg.m_start.m_z,
								   seg.m_end.m_x, seg.m_end.m_y, seg.m_end.m_z,
								   (seg.m_end - seg.m_start).magnitude());
						}
						std::string cause;
						double vol = -1.0, area = -1.0;
						try { vol = r.volume(); } catch (IBK::Exception & ex) { cause += "VOLUME[" + std::string(ex.what()) + "] "; }
						try { area = r.area(); } catch (IBK::Exception & ex) { cause += "AREA "; }
						printf("%-8s id=%-7u surfs=%-4zu uncovSegs=%-4zu uncovLen=%-8.2f vol=%-9.2f area=%-8.2f name='%s' %s\n",
							   sts, r.m_id, r.surfaces().size(), r.uncoveredSegments().size(),
							   uncovLen, vol, area,
							   r.m_displayName.toStdString().c_str(), cause.c_str());
					}
				}
			}
		}
		printf("SUMMARY %s: total=%d valid=%d warning=%d error=%d\n",
			   argv[1], nTotal, nValid, nWarn, nErr);
	}
	catch (IBK::Exception & ex) {
		ex.writeMsgStackToError();
		fprintf(stderr, "ERROR reading %s\n", argv[1]);
		return 2;
	}
	return 0;
}
