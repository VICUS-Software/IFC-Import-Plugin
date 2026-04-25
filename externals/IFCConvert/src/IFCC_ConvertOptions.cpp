#include "IFCC_ConvertOptions.h"

#include "IFCC_Helper.h"

namespace IFCC {

ConvertOptions::ConvertOptions() {
	m_noSearchForOpeningsInTypesFixed = constructionSimilarTypes();
	// IfcBuildingElementPart is in constructionSimilarTypes() but in some IFCs
	// (THO_optimized, BBW_Haus_D) it IS the wall geometry that hosts windows/doors
	// (e.g. "Mineralwolldämmung Prio 1" parts). Allow openings to match against
	// these — non-wall parts simply won't intersect any opening geometrically.
	m_noSearchForOpeningsInTypesFixed.remove(BET_BuildingElementPart);
	m_noSearchForOpeningsInTypesFixed += openingTypes();
	m_noSearchForOpeningsInTypesFixed << BET_Chimney << BET_RampFlight << BET_ShadingDevice;
	m_noSearchForOpeningsInTypesFixed << BET_Member << BET_Pile << BET_Plate << BET_Railing << BET_Ramp;
	m_noSearchForOpeningsInTypesFixed << BET_Stair << BET_StairFlight << BET_CivilElement << BET_DistributionElement;
	m_noSearchForOpeningsInTypesFixed << BET_ElementAssembly << BET_ElementComponent << BET_FeatureElement << BET_FurnishingElement;
	m_noSearchForOpeningsInTypesFixed << BET_GeographicalElement << BET_TransportElement << BET_VirtualElement;

	m_noSearchForOpeningsInTypes = m_noSearchForOpeningsInTypesFixed;
}

void ConvertOptions::addElementsForOpenings(const QSet<BuildingElementTypes> &newTypes) {
	m_noSearchForOpeningsInTypes = m_noSearchForOpeningsInTypesFixed;
	m_noSearchForOpeningsInTypes += newTypes;
}

} // namespace IFCC
