#ifndef IFCC_ConvertOptionsH
#define IFCC_ConvertOptionsH

#include <QSet>

#include "IFCC_Types.h"

namespace IFCC {

class ConvertOptions
{
public:
	enum ConstructionMatching {
		CM_MatchEachConstruction,
		CM_MatchOnlyFirstConstruction,
		CM_MatchFirstNConstructions,
		CM_NoMatching
	};

	ConvertOptions();

	bool hasElementsForSpaceBoundaries(BuildingElementTypes type) const {
		return m_elementsForSpaceBoundaries.find(type) != m_elementsForSpaceBoundaries.end();
	}

	bool noSearchForOpenings(BuildingElementTypes type) const {
		return m_noSearchForOpeningsInTypes.contains(type);
	}

	void addElementsForOpenings(const QSet<BuildingElementTypes>& newTypes);

	std::set<BuildingElementTypes>	m_elementsForSpaceBoundaries;
	double							m_distanceFactor = 3.0;
	double							m_standardWallThickness = 0.5;	///< Wall thickness which will be used in case no thickness is given
	double							m_openingDistance = 0.5;
	ConstructionMatching			m_matchingType = CM_MatchEachConstruction;
	int								m_matchedConstructionNumbers = 2;
	double							m_minimumSurfaceArea = 0.01;
	double							m_distanceEps = 1e-3;
	double							m_polygonEps = 1e-4;
	/*! AABB overlap tolerance [m] used by the fast matching prefilter.
		Independent of m_distanceEps, which governs plane-parallelism tests. */
	double							m_aabbExpandEps = 0.05;
	/*! If true, a candidate construction surface must face the space surface
		(dot(normal_space, normal_constr) < 0) and the space centroid must sit on the
		opposite side of the construction plane from where its normal points.
		Default OFF: many real-world IFCs author normals inconsistently, and enabling
		this filter can silently reject valid wall pairings, producing "Missing" SBs
		where walls should be. Opt-in for IFCs that are known to have clean normals. */
	bool							m_requireOppositeNormals = false;
	/*! Upper bound on the iterative subdivision loop in createSpaceBoundaries_2. */
	int								m_maxMatchIterations = 500;
	bool							m_createMissingSite = true;
	bool							m_writeConstructionElements = false;
	bool							m_writeBuildingElements = false;
	bool							m_writeOpeningElements = false;
	bool							m_writeOtherElements = false;
	bool							m_useCSGForOpenings = false;
	bool							m_useOldPolygonWriting = false;

private:
	QSet<BuildingElementTypes>		m_noSearchForOpeningsInTypes;
	QSet<BuildingElementTypes>		m_noSearchForOpeningsInTypesFixed;
};

} // namespace IFCC

#endif // IFCC_ConvertOptionsH
