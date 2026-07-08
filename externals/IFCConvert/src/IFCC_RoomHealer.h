#ifndef IFCC_RoomHealerH
#define IFCC_RoomHealerH

namespace VICUS {
	class Project;
}

namespace IFCC {

/*! Statistics of the room-geometry post-processing (see healRooms()). */
struct RoomHealStats {
	int m_roomsProcessed	= 0;
	int m_duplicatesDropped	= 0;	///< fully covered coplanar surfaces removed
	int m_surfacesClipped	= 0;	///< partially overlapping surfaces reduced to their remainder
	int m_surfacesFlipped	= 0;	///< winding corrections applied
	int m_holesClosed		= 0;	///< closing polygons added as Missing surfaces
	int m_subsurfacesDropped = 0;	///< broken window/door holes removed (untriangulatable parents)
	int m_fillsCoalesced	= 0;	///< coplanar Missing fill fragments merged away
	int m_roomsReverted		= 0;	///< safety net: healing made the room worse -> undone
	int m_errBefore = 0, m_errAfter = 0;
	int m_warnBefore = 0, m_warnAfter = 0;
};

/*! Structured post-processing over the final VICUS room geometry. Runs once at the
	end of the conversion (buildVicusProject) and repairs the room shells:
	 - pass 1: coplanar duplicate surfaces are dropped, partial overlaps clipped away
	   (surfaces carrying subsurfaces are protected and never modified);
	 - pass 2: winding repair — orientation consistency is propagated over shared
	   polygon edges, each connected component is flipped so its signed volume
	   contribution w.r.t. the room center is outward;
	 - pass 3: remaining shell holes are closed with the room's closing polygons
	   (VICUS::Room::closingPolygons), added as 'Missing' surfaces.
	Per room a safety net compares the VICUS::Room::roomStatus before and after —
	if healing made the status worse, ALL changes of that room are reverted.
	Kill-switch: environment variable IFCC_NO_ROOMHEAL disables the whole pass. */
RoomHealStats healRooms(VICUS::Project& prj);

} // namespace IFCC

#endif // IFCC_RoomHealerH
