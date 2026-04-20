#include "IFCC_Space.h"

#include <ifcpp/IFC4X3/include/IfcRelSpaceBoundary.h>
#include <ifcpp/IFC4X3/include/IfcLengthMeasure.h>
#include <ifcpp/IFC4X3/include/IfcElementCompositionEnum.h>

#include <numeric>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>

#include <IBK_math.h>
#include <IBK_FormatString.h>

#include <IBKMK_3DCalculations.h>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_Cancellation.h"

namespace IFCC {

Space::Space(int id) :
	EntityBase(id)
{
}

bool Space::set(std::shared_ptr<IFC4X3::IfcSpace> ifcSpace, std::vector<ConvertError>& errors) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcSpace)))
		return false;

	m_longName = label2s(ifcSpace->m_LongName);
	if(ifcSpace->m_PredefinedType != nullptr)
		m_spaceType = ifcSpace->m_PredefinedType->m_enum;

	if(ifcSpace->m_CompositionType != nullptr) {
		switch(ifcSpace->m_CompositionType->m_enum) {
			case IFC4X3::IfcElementCompositionEnum::ENUM_COMPLEX: m_compositionType = CT_Complex; break;
			case IFC4X3::IfcElementCompositionEnum::ENUM_ELEMENT: m_compositionType = CT_Element; break;
			case IFC4X3::IfcElementCompositionEnum::ENUM_PARTIAL: m_compositionType = CT_Partial; break;
		}
	}

	// look for space boundaries from IFC
	for( const auto& bound : ifcSpace->m_BoundedBy_inverse) {
		auto boundP = bound.lock();
		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		bool res = sb->setFromIFC(boundP, errors);
		if(res) {
			m_spaceBoundaries.push_back(sb);
			m_spaceBoundaryGUIDs.push_back(sb->m_guid);
		}
	}

	for( const auto& cover : ifcSpace->m_HasCoverings_inverse) {
		auto coverP = cover.lock();
	}

	getSpaceProperties(ifcSpace, m_properties);

	return true;
}

void Space::update(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	transform(productShape);
	fetchGeometry(productShape, errors);
}


void Space::transform(std::shared_ptr<ProductShapeData> productShape) {
	if(productShape == nullptr)
		return;

	m_transformMatrix = productShape->getTransform();
	if(m_transformMatrix != carve::math::Matrix::IDENT()) {
		productShape->applyTransformToProduct(m_transformMatrix, true, true);
	}
}

void Space::fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors) {
	if(productShape == nullptr)
		return;

	surfacesFromRepresentation(productShape, m_surfacesOrg, errors, OT_Space, m_id);

	m_meshSets = meshSetsFromBodyRepresentation(productShape);

	if(m_surfacesOrg.empty())
		return;
}


static bool divideSurface(const Surface::IntersectionResult& intRes, std::vector<Surface>& spaceSurfaces, int ssIndex, std::vector<Surface>& subsurfaces) {
	// we don't have any intersections
	if(intRes.m_intersections.empty())
		return false;

	// construction element covers complete space surface
	if(intRes.m_diffBaseMinusClip.empty()) {
//		spaceSurfaces.erase(spaceSurfaces.begin() + ssIndex);
		return true;
	}

	// we have a rest of the space surface without matching construction element
	// we have only one resulting space surface - change original surface to it
	std::vector<Surface> diffSurfaces;
	for(size_t i=0; i<intRes.m_diffBaseMinusClip.size(); ++i) {
		Surface surf = intRes.m_diffBaseMinusClip[i];
		std::vector<Surface> tmp = surf.getSimplified();

		// should never happen
		if(tmp.empty())
			return false;

		// we have some holes - add these to subsurface list
		if(!intRes.m_holesBaseMinusClip[i].empty()) {
			for(const Surface& subsurf : intRes.m_holesBaseMinusClip[i])
				subsurfaces.push_back(subsurf);
		}
		for(const auto& s : tmp) {
			diffSurfaces.push_back(s);
		}
	}

	spaceSurfaces[ssIndex] = diffSurfaces.front();
	for(size_t i=1; i<diffSurfaces.size(); ++i) {
		spaceSurfaces.push_back(diffSurfaces[i]);
	}
	return false;
}

/*! Struct contains result of the function findFirstSurfaceMatchIndex.
	It is used for matching a construction surface to a space surface.*/
struct MatchResult {
	/*! Default constructor creates a non valid object.*/
	MatchResult() :
		m_wallSurfaceIndex(-1),
		m_spaceSurfaceIndex(-1)
	{}

	/*! Standard constructor.*/
	MatchResult(int wallSurfaceIndex, int spaceSurfaceIndex) :
		m_wallSurfaceIndex(wallSurfaceIndex),
		m_spaceSurfaceIndex(spaceSurfaceIndex)
	{}

	/*! Return true if both indices are valid (greater than -1).*/
	bool isValid() const {
		return m_wallSurfaceIndex > -1 && m_spaceSurfaceIndex > -1;
	}

	int m_wallSurfaceIndex;		///< Index of the construction surface
	int m_spaceSurfaceIndex;	///< Index of the space surface
};

static MatchResult findFirstSurfaceMatchIndex(const std::vector<Surface>& wallSurfaces, const std::vector<Surface>& spaceSurfaces, double minDist,
											  const ConvertOptions& convertOptions) {
	const double EPS = convertOptions.m_distanceEps;
	for(size_t wi=0; wi<wallSurfaces.size(); ++wi) {
		const Surface& wallSurf = wallSurfaces[wi];
		for(size_t si=0; si<spaceSurfaces.size(); ++si) {
			const Surface& spaceSurf = spaceSurfaces[si];
			if(spaceSurf.isParallelTo(wallSurf, convertOptions.m_distanceEps)) {
				double dist = spaceSurf.distanceToParallelPlane(wallSurf, convertOptions.m_distanceEps);
				if(dist < minDist * (1+EPS)) {
					if(wallSurf.isIntersected(spaceSurf))
						return MatchResult(wi,si);
				}
			}
		}
	}
	return MatchResult();
}


std::vector<std::shared_ptr<SpaceBoundary>> Space::createSpaceBoundaries(const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																		 const ConvertOptions& convertOptions) {
	std::vector<Surface> surfaces(m_surfacesOrg);
	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();


	for(const auto& construction : constructionElements) {
		double dist = construction->thickness();
		double maxConstructionDist = 0;
		if(construction->isSubSurfaceComponent() && !construction->m_openingProperties.m_constructionThicknesses.empty()) {
			maxConstructionDist = *std::max_element(construction->m_openingProperties.m_constructionThicknesses.begin(),
												   construction->m_openingProperties.m_constructionThicknesses.end());
		}
		if(dist < convertOptions.m_distanceEps) {
			if(maxConstructionDist > convertOptions.m_distanceEps)
				dist = maxConstructionDist;
			else
				dist = convertOptions.m_standardWallThickness;
		}

		// try to find a construction surface an a space surface which matches together
		MatchResult indices = findFirstSurfaceMatchIndex(construction->surfaces(), surfaces, dist*convertOptions.m_distanceFactor, convertOptions);
		if(indices.isValid()) {
			int loopCount = 0;
			do {
				const Surface& currSpaceSurf = surfaces[indices.m_spaceSurfaceIndex];
				const Surface& currConstSurf = construction->surfaces()[indices.m_wallSurfaceIndex];
				++loopCount;
				Surface::IntersectionResult intersectionResult = currSpaceSurf.intersect2(currConstSurf);
				// no intersections found
				if(intersectionResult.m_intersections.empty())
					break;

				for(size_t i=0; i<intersectionResult.m_intersections.size(); ++i) {
					if(!intersectionResult.m_holesIntersections[i].empty()) {
//						errors.push_back(ConvertError{OT_Space, m_id, IBK::FormatString("intersection from space surface and building element surface has %1 holes")
//													  .arg(intersectionResult.m_holesIntersections[i].size()).str()});
					}
				}

				if(intersectionResult.holesWithChilds() > 0) {
					errors.push_back(ConvertError{OT_Space, m_id, "one or more holes in intersections or diff surface has childs"});
				}

				const Surface& firstISurf = intersectionResult.m_intersections.front();
				if(intersectionResult.m_intersections.size() == 1 && IBK::nearly_equal<2>(firstISurf.area(),currSpaceSurf.area())) {
					int id = GUID_maker::instance().guid();
					std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
					std::string name = m_name + ":" + construction->m_name + " - " +
							std::to_string(indices.m_spaceSurfaceIndex) +
							" : " + std::to_string(indices.m_wallSurfaceIndex);
					sb->setFromBuildingElement(name, construction, *this);
					sb->m_elementEntityId = construction->m_id;
					sb->fetchGeometryFromBuildingElement(firstISurf, convertOptions);
					spaceBoundaries.push_back(sb);
					surfaces.erase(surfaces.begin() + indices.m_spaceSurfaceIndex);
				}
				else {
					for(const Surface& surf : intersectionResult.m_intersections) {
						int id = GUID_maker::instance().guid();
						std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
						std::string name = m_name + ":" + construction->m_name + " - " +
								std::to_string(indices.m_spaceSurfaceIndex) + " : " + std::to_string(indices.m_wallSurfaceIndex);
						sb->setFromBuildingElement(name, construction, *this);
						sb->m_elementEntityId = construction->m_id;
						sb->fetchGeometryFromBuildingElement(surf, convertOptions);
						spaceBoundaries.push_back(sb);
					}
					std::vector<Surface> subsurfaces;
					// add difference surface - intersections to the surface list and remove the original one
					divideSurface(intersectionResult, surfaces, indices.m_spaceSurfaceIndex, subsurfaces);
					if(!subsurfaces.empty()) {
						// what should we do with the holes?
						errors.push_back(ConvertError{OT_Space, m_id, "rest surface from intersection from space surface and building element surface has holes"});
					}
				}
				indices = findFirstSurfaceMatchIndex(construction->surfaces(), surfaces, dist*convertOptions.m_distanceFactor, convertOptions);

				if(loopCount > 1000) {
					errors.push_back(ConvertError{OT_Space, m_id, "more than 1000 intersections found"});
					break;
				}
			} while(indices.isValid());
		}
	}

	for(const Surface& surf : surfaces) {
		if(surf.area() < 0.01)
			continue;

		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		std::string name = "Missing";
		sb->setForMissingElement(name, *this, false);
		sb->fetchGeometryFromBuildingElement(surf, convertOptions);
		spaceBoundaries.push_back(sb);
	}

	return spaceBoundaries;
}

struct ConstructionSurfaceInfo {
	int								m_id = -1;
	int								m_surfaceIndex = -1;
	BuildingElementTypes			m_type = IFCC::BET_None;
	double							m_distance = 0;
	double							m_intersectionArea = 0;

	/*! Cached result of intersect2 from the prefilter. Reused by the caller to avoid
		running the expensive Boolean operation twice on the same pair. */
	Surface::IntersectionResult		m_intersectionResult;
	bool							m_hasCachedIntersection = false;

	bool isValid() const { return m_id > -1; }
};

struct SpaceSurfaceMatches {
	int										m_spaceSurfaceIndex;
	std::vector<ConstructionSurfaceInfo>	m_constructions;
};

// Orientation bucket for a plane normal. 0=X-dominant, 1=Y-dominant, 2=Z-dominant, 3=sloped.
static int classifyOrientation(const IBKMK::Vector3D& n) {
	double ax = std::fabs(n.m_x);
	double ay = std::fabs(n.m_y);
	double az = std::fabs(n.m_z);
	// Threshold 0.9 keeps only near-axis-aligned surfaces in X/Y/Z buckets.
	// Anything more tilted lands in the sloped bucket and is tested against everything.
	const double AXIS_THRESHOLD = 0.9;
	if(ax >= AXIS_THRESHOLD && ax >= ay && ax >= az) return 0;
	if(ay >= AXIS_THRESHOLD && ay >= ax && ay >= az) return 1;
	if(az >= AXIS_THRESHOLD && az >= ax && az >= ay) return 2;
	return 3;
}

// Flat index entry over all construction surfaces — populated once per createSpaceBoundaries_2 call.
// Stores (constrElementIdx, surfaceIdx) rather than a raw Surface pointer so the entry
// remains valid even if construction surface vectors are copied/moved elsewhere.
struct IndexedConstrSurface {
	int							m_constructionElementIdx;	///< Index into the outer constructionElements vector
	int							m_constructionSurfaceIdx;	///< Surface index within construction->surfaces()
	int							m_constructionId;			///< Real id used by buildingElements.fromID
	double						m_threshDist;				///< Plane-distance threshold for this construction (thickness × factor)
	BuildingElementTypes		m_type;
};

// Weighted score used to pick the single best matching construction surface for a given space surface.
// Lower score = better match. Combines normalized area (bigger = better), distance (smaller = better),
// and a type-priority penalty (walls/roofs preferred over beams/coverings).
static double matchScore(const ConstructionSurfaceInfo& c, double maxArea, double maxDist) {
	double areaScore = (maxArea > 0.0) ? (maxArea - c.m_intersectionArea) / maxArea : 0.0;
	double distScore = (maxDist > 0.0) ? (c.m_distance / maxDist) : 0.0;
	double typeScore;
	switch(c.m_type) {
		case BET_Wall:
		case BET_Roof:		typeScore = 0.0; break;
		case BET_Slab:		typeScore = 0.1; break;
		case BET_Beam:
		case BET_Covering:	typeScore = 0.5; break;
		default:			typeScore = 0.8; break;
	}
	return 1.0 * areaScore + 0.5 * distScore + 0.3 * typeScore;
}

// For each space surface, find the single best matching construction surface using
// the pre-built orientation-bucketed index. Candidate filtering order: AABB overlap,
// plane parallelism, optional opposite-normal + side-of-space check, plane distance,
// full Boolean intersection (intersect2). The IntersectionResult is cached inside the
// returned ConstructionSurfaceInfo so the caller can skip the expensive second call.
static ConstructionSurfaceInfo findBestMatchUsingIndex(const Surface& spaceSurface,
													   const IBKMK::Vector3D& spaceCentroid,
													   const std::array<std::vector<IndexedConstrSurface>, 4>& buckets,
													   const std::vector<std::shared_ptr<BuildingElement>>& constructionElements,
													   const ConvertOptions& convertOptions) {
	const double EPS = convertOptions.m_distanceEps;
	IBKMK::Vector3D ssNormal = spaceSurface.planeNormalVec();
	int ssBucket = classifyOrientation(ssNormal);

	std::vector<ConstructionSurfaceInfo> candidates;
	// Visit the matching axis bucket plus the sloped bucket. When the space surface is itself
	// sloped, it's already in bucket 3 and we skip the redundant re-visit.
	std::array<int, 2> bucketsToVisit = { ssBucket, 3 };
	int visitCount = (ssBucket == 3) ? 1 : 2;

	for(int vi = 0; vi < visitCount; ++vi) {
		int bi = bucketsToVisit[vi];
		const std::vector<IndexedConstrSurface>& bucket = buckets[bi];
		for(const IndexedConstrSurface& ics : bucket) {
			const Surface& cs = constructionElements[ics.m_constructionElementIdx]->surfaces()[ics.m_constructionSurfaceIdx];

			double aabbEps = convertOptions.m_aabbExpandEps + ics.m_threshDist;
			if(!spaceSurface.aabbOverlaps(cs, aabbEps))
				continue;

			if(!spaceSurface.isParallelTo(cs, EPS))
				continue;

			if(convertOptions.m_requireOppositeNormals) {
				IBKMK::Vector3D csN = cs.planeNormalVec();
				double dot = ssNormal.m_x * csN.m_x + ssNormal.m_y * csN.m_y + ssNormal.m_z * csN.m_z;
				// Space-surface normals point outward from the space; a true matching
				// construction surface faces the space (opposite direction) → dot < 0.
				if(dot >= 0.0)
					continue;

				// Side-of-space geometric check: the space centroid must lie on the side
				// of the construction plane AWAY from where the construction normal points.
				// This is robust to inconsistent normal authoring — it depends only on the
				// construction normal pointing outward from the solid, a common convention.
				const IBKMK::Vector3D& csCenter = cs.centroid();
				double sideDot = csN.m_x * (spaceCentroid.m_x - csCenter.m_x)
							   + csN.m_y * (spaceCentroid.m_y - csCenter.m_y)
							   + csN.m_z * (spaceCentroid.m_z - csCenter.m_z);
				if(sideDot >= 0.0)
					continue;
			}

			double dist = spaceSurface.distanceToParallelPlane(cs, EPS);
			if(dist >= ics.m_threshDist * (1.0 + EPS))
				continue;

			// Full Boolean intersection — expensive, but avoids a second call in the caller.
			Surface::IntersectionResult result = spaceSurface.intersect2(cs);
			if(!result.isValid())
				continue;

			double area = 0.0;
			for(const Surface& s : result.m_intersections)
				area += s.area();
			if(area <= convertOptions.m_minimumSurfaceArea)
				continue;

			ConstructionSurfaceInfo c;
			c.m_id = ics.m_constructionId;
			c.m_type = ics.m_type;
			c.m_distance = dist;
			c.m_intersectionArea = area;
			c.m_surfaceIndex = ics.m_constructionSurfaceIdx;
			c.m_intersectionResult = std::move(result);
			c.m_hasCachedIntersection = true;
			candidates.push_back(std::move(c));
		}
	}

	if(candidates.empty())
		return ConstructionSurfaceInfo();

	// Single-pass weighted scoring replaces the old double-sort.
	double maxArea = 0.0, maxDist = 0.0;
	for(const ConstructionSurfaceInfo& c : candidates) {
		if(c.m_intersectionArea > maxArea) maxArea = c.m_intersectionArea;
		if(c.m_distance > maxDist) maxDist = c.m_distance;
	}
	size_t bestIdx = 0;
	double bestScore = matchScore(candidates[0], maxArea, maxDist);
	for(size_t i = 1; i < candidates.size(); ++i) {
		double s = matchScore(candidates[i], maxArea, maxDist);
		if(s < bestScore) { bestScore = s; bestIdx = i; }
	}
	return std::move(candidates[bestIdx]);
}

static std::shared_ptr<SpaceBoundary> createSpaceBoundary(const std::shared_ptr<BuildingElement>& constr, const SpaceSurfaceMatches& match,
														  int constrSurfIndex, const Space& space, const Surface& intersection, const ConvertOptions& convertOptions ) {
	int id = GUID_maker::instance().guid();
	std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(id));
	std::string name = space.m_name + ":" + constr->m_name + " - " +
			std::to_string(match.m_spaceSurfaceIndex) +
			" : " + std::to_string(constrSurfIndex);
	sb->setFromBuildingElement(name, constr, space);
	sb->m_elementEntityId = constr->m_id;
	sb->fetchGeometryFromBuildingElement(intersection, convertOptions);
	return sb;
}

std::vector<std::shared_ptr<SpaceBoundary>> Space::createSpaceBoundaries_2(const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																		   const ConvertOptions& convertOptions) {
	std::vector<Surface> surfaces(m_surfacesOrg);
	std::vector<std::shared_ptr<SpaceBoundary>> spaceBoundaries;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();

	ConvertOptions::ConstructionMatching matchType = convertOptions.m_matchingType;

	if(matchType != ConvertOptions::CM_NoMatching) {

		// Pre-warm Surface AABB caches for the space surfaces — safe here because this runs
		// from the outer per-space OMP region; each space owns its own surfaces exclusively.
		for(Surface& s : surfaces) {
			s.aabbMin();
			s.aabbMax();
		}

		// Build the orientation-bucketed construction surface index ONCE.
		// Buckets: 0 = X-dominant normal, 1 = Y-dominant, 2 = Z-dominant, 3 = sloped.
		// A space surface only needs to check its own axis bucket plus the sloped bucket.
		std::array<std::vector<IndexedConstrSurface>, 4> buckets;
		for(size_t ci = 0; ci < constructionElements.size(); ++ci) {
			const auto& construction = constructionElements[ci];
			BuildingElementTypes type = construction->type();
			if(!convertOptions.hasElementsForSpaceBoundaries(type))
				continue;

			double dist = construction->thickness();
			double maxConstructionDist = 0;
			if(construction->isSubSurfaceComponent() && !construction->m_openingProperties.m_constructionThicknesses.empty()) {
				maxConstructionDist = *std::max_element(construction->m_openingProperties.m_constructionThicknesses.begin(),
														construction->m_openingProperties.m_constructionThicknesses.end());
			}
			const double MIN_THICKNESS = convertOptions.m_standardWallThickness;
			if(dist < MIN_THICKNESS) {
				if(maxConstructionDist > MIN_THICKNESS)
					dist = maxConstructionDist;
				else
					dist = MIN_THICKNESS;
			}
			dist *= convertOptions.m_distanceFactor;

			const std::vector<Surface>& csList = construction->surfaces();
			for(size_t si=0; si<csList.size(); ++si) {
				const Surface& cs = csList[si];
				// Populate AABB + centroid caches serially before any parallel reads.
				cs.aabbMin();
				cs.aabbMax();
				cs.centroid();
				int bi = classifyOrientation(cs.planeNormalVec());
				buckets[bi].push_back(IndexedConstrSurface{
					(int)ci, (int)si, construction->m_id, dist, type
				});
			}
		}

		// Compute this space's centroid (average of all original surface centroids) once —
		// used as a stable point for the side-of-plane geometric test.
		IBKMK::Vector3D spaceCentroid(0, 0, 0);
		{
			size_t npts = 0;
			for(const Surface& s : m_surfacesOrg) {
				const IBKMK::Vector3D& c = s.centroid();
				spaceCentroid = IBKMK::Vector3D(spaceCentroid.m_x + c.m_x,
												spaceCentroid.m_y + c.m_y,
												spaceCentroid.m_z + c.m_z);
				++npts;
			}
			if(npts > 0)
				spaceCentroid = IBKMK::Vector3D(spaceCentroid.m_x / double(npts),
												spaceCentroid.m_y / double(npts),
												spaceCentroid.m_z / double(npts));
		}

		const int PER_SURFACE_MAX = matchType == ConvertOptions::CM_MatchOnlyFirstConstruction
				? 1
				: (matchType == ConvertOptions::CM_MatchFirstNConstructions
					? std::max(1, convertOptions.m_matchedConstructionNumbers)
					: std::numeric_limits<int>::max());

		// Total-work safety net to guarantee termination on pathological inputs.
		const int TOTAL_MAX_ITER = std::max(1, convertOptions.m_maxMatchIterations)
				* std::max<int>(1, (int)m_surfacesOrg.size());

		// Work queue of surface indices. Residuals produced by divideSurface are appended
		// to `surfaces` and pushed into the queue so they're matched in the same pass —
		// no more outer do-while rebuilding `matches` from scratch.
		std::deque<size_t> workQueue;
		for(size_t i=0; i<surfaces.size(); ++i)
			workQueue.push_back(i);

		// Per-surface-slot match counter. Residuals inherit their parent slot's count.
		std::vector<int> matchCount(surfaces.size(), 0);

		int iters = 0;
		while(!workQueue.empty() && iters < TOTAL_MAX_ITER) {
			// Honor user cancellation — bail out leaving remaining surfaces to be marked "Missing".
			if(Cancellation::isCancelled())
				break;
			++iters;
			size_t ssi = workQueue.front();
			workQueue.pop_front();

			// Skip entries that were consumed (polygon cleared) or are too small.
			if(surfaces[ssi].polygon().empty() || surfaces[ssi].area() < convertOptions.m_minimumSurfaceArea)
				continue;

			if(matchCount[ssi] >= PER_SURFACE_MAX)
				continue;

			ConstructionSurfaceInfo best = findBestMatchUsingIndex(
				surfaces[ssi], spaceCentroid, buckets, constructionElements, convertOptions);
			if(!best.isValid())
				continue;

			++matchCount[ssi];

			std::shared_ptr<BuildingElement> constr = buildingElements.fromID(best.m_id);
			int bestConstrSurfaceIndex = best.m_surfaceIndex;

			// Reuse the intersection result from the prefilter.
			Surface::IntersectionResult intersectionResult;
			if(best.m_hasCachedIntersection)
				intersectionResult = std::move(best.m_intersectionResult);
			else
				intersectionResult = surfaces[ssi].intersect2(constr->surfaces()[bestConstrSurfaceIndex]);

			if(intersectionResult.m_intersections.empty())
				continue;

			if(intersectionResult.holesWithChilds() > 0)
				errors.push_back(ConvertError{OT_Space, m_id, "one or more holes in intersections or diff surface has childs"});

			SpaceSurfaceMatches matchStub;
			matchStub.m_spaceSurfaceIndex = (int)ssi;

			// CM_MatchOnlyFirstConstruction: one SB for the whole remaining surface, no subdivision.
			if(convertOptions.m_matchingType == ConvertOptions::CM_MatchOnlyFirstConstruction) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, surfaces[ssi], convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// CM_MatchFirstNConstructions: on the Nth (final) allowed match, emit a whole-surface SB
			// instead of subdividing — preserves original algorithm's end-of-budget behavior.
			bool finalBudgetReached = (convertOptions.m_matchingType == ConvertOptions::CM_MatchFirstNConstructions)
					&& (matchCount[ssi] >= PER_SURFACE_MAX);
			if(finalBudgetReached) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, surfaces[ssi], convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// Full-coverage shortcut: intersection equals the space surface exactly.
			const Surface& firstISurf = intersectionResult.m_intersections.front();
			double spaceArea = surfaces[ssi].area();
			if(intersectionResult.m_intersections.size() == 1
					&& IBK::nearly_equal<2>(firstISurf.area(), spaceArea)) {
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, firstISurf, convertOptions));
				surfaces[ssi].setNewPolygon({});
				continue;
			}

			// Partial coverage: emit SB per intersection, subdivide, enqueue residuals.
			for(const Surface& surf : intersectionResult.m_intersections)
				spaceBoundaries.push_back(createSpaceBoundary(constr, matchStub, bestConstrSurfaceIndex,
															  *this, surf, convertOptions));

			std::vector<Surface> holeSubsurfaces;
			size_t nSurfacesBefore = surfaces.size();
			bool fullyConsumed = divideSurface(intersectionResult, surfaces, (int)ssi, holeSubsurfaces);

			if(fullyConsumed) {
				surfaces[ssi].setNewPolygon({});
			}
			else {
				// divideSurface replaced surfaces[ssi] with the first residual and appended the rest.
				// Residuals inherit matchCount from parent (same budget down the subdivision chain).
				int parentCount = matchCount[ssi];
				if(matchCount.size() < surfaces.size())
					matchCount.resize(surfaces.size(), parentCount);
				for(size_t newIdx = nSurfacesBefore; newIdx < surfaces.size(); ++newIdx) {
					matchCount[newIdx] = parentCount;
					workQueue.push_back(newIdx);
				}
				// The parent slot itself now holds a residual — requeue it for further matching.
				workQueue.push_back(ssi);
			}

			if(!holeSubsurfaces.empty())
				errors.push_back(ConvertError{OT_Space, m_id, "rest surface from intersection from space surface and building element surface has holes"});
		}

		if(iters >= TOTAL_MAX_ITER)
			errors.push_back(ConvertError{OT_Space, m_id, "space-boundary matching hit iteration cap — some surfaces may be marked missing"});
	}

	for(const Surface& surf : surfaces) {
		if(surf.area() < convertOptions.m_minimumSurfaceArea)
			continue;

		std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
		std::string name = "Missing";
		sb->setForMissingElement(name, *this, false);
		sb->fetchGeometryFromBuildingElement(surf, convertOptions);
		spaceBoundaries.push_back(sb);
	}

	return spaceBoundaries;
}

static Surface matchingOpeningSurface(const Surface& currentOpeningSurf, const std::shared_ptr<SpaceBoundary> spaceBoundary,
									  const ConvertOptions& convertOptions, double maxDistance) {
	// NOTE: an AABB prefilter was tried here but removed — opening polygons and wall SBs
	// that legitimately contain each other can have surprising AABB gaps (very thin openings,
	// inconsistent coordinate conventions), and the distanceToParallelPlane test below is
	// already cheap enough that the prefilter is not worth the risk of silently dropping windows.

	double dist = currentOpeningSurf.distanceToParallelPlane(spaceBoundary->surface(), convertOptions.m_distanceEps);
	if(dist > maxDistance)
		return Surface();

	Surface intersectionResult = spaceBoundary->surface().intersect(currentOpeningSurf);
	if(intersectionResult.isValid(convertOptions.m_distanceEps))
		return intersectionResult;

	return Surface();
}

static Surface mergeSurfaces(const std::vector<Surface>& surfaces, double eps) {
	Surface res = surfaces.front();
	for(size_t i=1; i<surfaces.size(); ++i) {
		res.mergeOnlyThanPlanar(surfaces[i], eps);
	}
	return res;
}

static bool addOpeningSpaceBoundary(const Surface& surface, Opening& currOp, const std::shared_ptr<SpaceBoundary> spaceBoundary, std::shared_ptr<BuildingElement> openingElem,
				  const std::string& spaceName, std::vector<std::shared_ptr<SpaceBoundary>>& openingSpaceBoundaries, const Space& space, const ConvertOptions& convertOptions) {
	if(!surface.isValid(convertOptions.m_distanceEps))
		return false;

	std::shared_ptr<SpaceBoundary> sb = std::shared_ptr<SpaceBoundary>(new SpaceBoundary(GUID_maker::instance().guid()));
	if(openingElem) {
		std::string name = spaceName + ":" + spaceBoundary->m_name+ ":" + openingElem->m_name + " - O" +
				std::to_string(openingElem->m_id) + " : OS" +
				std::to_string(surface.id());
		sb->setFromBuildingElement(name, openingElem, space);
		sb->m_elementEntityId = openingElem->m_id;
		sb->m_openingId = currOp.m_id;
		sb->fetchGeometryFromBuildingElement(surface, convertOptions);
		openingSpaceBoundaries.push_back(sb);
		currOp.setSpaceBoundary(sb);
	}
	else {
		std::string name = spaceName + spaceBoundary->m_name+ ":" + ": breakout - O" +
				std::to_string(-1) + " : OS" +
				std::to_string(surface.id());
		sb->setForVirtualElement(name, space, true);
		sb->m_openingId = currOp.m_id;
		sb->fetchGeometryFromBuildingElement(surface, convertOptions);
		openingSpaceBoundaries.push_back(sb);
		currOp.setSpaceBoundary(sb);
	}
	spaceBoundary->addContainedOpeningSpaceBoundaries(sb);
	// we found a connection therfore we can end searching
	return true;
}

static void searchOpeningSpaceBoundaries(Opening& currOp, const std::shared_ptr<SpaceBoundary> spaceBoundary, const std::shared_ptr<BuildingElement>& openingElem,
								  const ConvertOptions& convertOptions, std::vector<std::shared_ptr<SpaceBoundary>>& openingSpaceBoundaries,
								  const Space& space, double maxDistance) {
	std::vector<Surface> openingSurfaces;
	const std::vector<Surface>& openingSurfces = convertOptions.m_useCSGForOpenings && !currOp.surfacesCSGElement().empty() ? currOp.surfacesCSGElement() :
																													   currOp.surfaces();	
	for(size_t cosi=0; cosi<openingSurfces.size(); ++cosi) {
		const Surface& currentOpeningSurf = openingSurfces[cosi];
		if(currentOpeningSurf.sideType() != Surface::ST_ProbableSide)
			continue;

		Surface surf = matchingOpeningSurface(currentOpeningSurf, spaceBoundary, convertOptions, maxDistance);
		if(surf.isValid(convertOptions.m_distanceEps))
			openingSurfaces.push_back(surf);
	}
	if(openingSurfaces.empty()) {
		for(size_t cosi=0; cosi<currOp.surfaces().size(); ++cosi) {
			const Surface& currentOpeningSurf = currOp.surfaces()[cosi];

			Surface surf = matchingOpeningSurface(currentOpeningSurf, spaceBoundary, convertOptions, maxDistance);
			if(surf.isValid(convertOptions.m_distanceEps))
				openingSurfaces.push_back(surf);
		}
	}
	if(!openingSurfaces.empty()) {
		Surface mergedSurface = mergeSurfaces(openingSurfaces, convertOptions.m_distanceEps);
		addOpeningSpaceBoundary(mergedSurface, currOp, spaceBoundary, openingElem, space.m_longName, openingSpaceBoundaries, space, convertOptions);
	}
}


void Space::createSpaceBoundariesForOpeningsFromSpaceBoundaries(std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
																const BuildingElementsCollector& buildingElements,
																std::vector<Opening>& openings, std::vector<ConvertError>& errors,
																const ConvertOptions& convertOptions) {
	if(openings.empty())
		return;

	std::vector<std::shared_ptr<SpaceBoundary>> openingSpaceBoundaries;

	for(const auto& spaceBoundary : spaceBoundaries) {
		// openings can only be part of a construction space boundary
		if(!spaceBoundary->isConstructionElement())
			continue;

		std::string elemGUID = spaceBoundary->guidRelatedElement();
		const std::shared_ptr<BuildingElement> elem = buildingElements.fromGUID(elemGUID);
		// only go further if space boundary is connected to a existing construction element
		if(elem.get() == nullptr)
			continue;

		// only go further if the construction contains openings
		if(elem->m_containedOpenings.empty())
			continue;

		if(convertOptions.noSearchForOpenings(spaceBoundary->typeRelatedElement()))
			continue;

		// extend search distance by construction thickness (thick walls can place openings beyond default 0.5m)
		double searchDist = convertOptions.m_openingDistance;
		searchDist = std::max(elem->thickness(), searchDist);
		searchDist *= 1.1;

		// collect all contained openings
		std::vector<size_t> containedOpeningsIndices;
		for(int opid : elem->m_containedOpenings) {
			auto fitOp = std::find_if(openings.begin(), openings.end(),
									  [opid](const auto& op) -> bool { return op.m_id == opid; });
			if(fitOp != openings.end())
				containedOpeningsIndices.push_back(std::distance(openings.begin(), fitOp));
		}
		// only go further if we have some contained openings
		if(containedOpeningsIndices.empty())
			continue;

		// look for all openings which are related to the construction element of the space boundary
		for(size_t coi=0; coi<containedOpeningsIndices.size(); ++coi) {
			int opIndex = containedOpeningsIndices[coi];
			Opening& currOp = openings[opIndex];
			if(currOp.hasSpaceBoundary())
				continue;

			std::shared_ptr<BuildingElement> openingElem;
			// has no construction - its a breakout
			if(currOp.openingElementIds().size() == 1) {
				int id = currOp.openingElementIds().front();
				openingElem = buildingElements.fromID(id);
			}
			// multiple opening elements (e.g. curtain wall) - pick first window/door, fallback to first element
			else if(currOp.openingElementIds().size() > 1) {
				for(int id : currOp.openingElementIds()) {
					std::shared_ptr<BuildingElement> elem = buildingElements.fromID(id);
					if(elem && isOpeningType(elem->type())) {
						openingElem = elem;
						break;
					}
				}
				if(!openingElem) {
					int id = currOp.openingElementIds().front();
					openingElem = buildingElements.fromID(id);
				}
			}
			searchOpeningSpaceBoundaries(currOp, spaceBoundary, openingElem, convertOptions, openingSpaceBoundaries, *this, searchDist);
		}
	}

	// now look for all non related openings (openings not found via m_containedOpenings)
	for(Opening& currOp : openings) {
		if(currOp.hasSpaceBoundary())
			continue;

		for(const auto& spaceBoundary : spaceBoundaries) {
			// Re-check per inner iteration — once a match is made for this opening via a prior SB,
			// we must stop, otherwise searchOpeningSpaceBoundaries is called again for the same
			// opening and creates a duplicate opening-SB linked to a second parent SB.
			if(currOp.hasSpaceBoundary())
				break;

			// openings can only be part of a construction space boundary
			if(!spaceBoundary->isConstructionElement())
				continue;

			if(convertOptions.noSearchForOpenings(spaceBoundary->typeRelatedElement()))
				continue;

			// get the construction element for this space boundary
			std::string elemGUID = spaceBoundary->guidRelatedElement();
			const std::shared_ptr<BuildingElement> sbElem = buildingElements.fromGUID(elemGUID);

			// only match this opening if the SB's construction element actually contains it
			// this prevents matching openings to the wrong wall (e.g. exterior window matched to interior wall)
			if(sbElem) {
				auto& ops = sbElem->m_containedOpenings;
				if(std::find(ops.begin(), ops.end(), currOp.m_id) == ops.end())
					continue;
			}

			// extend search distance by construction thickness
			double searchDist = convertOptions.m_openingDistance;
			if(sbElem)
				searchDist = std::max(sbElem->thickness(), searchDist);
			searchDist *= 1.1;

			std::shared_ptr<BuildingElement> openingElem;
			// has no construction - its a breakout
			if(currOp.openingElementIds().size() == 1) {
				int id = currOp.openingElementIds().front();
				openingElem = buildingElements.fromID(id);
			}
			// multiple opening elements (e.g. curtain wall) - pick first window/door, fallback to first element
			else if(currOp.openingElementIds().size() > 1) {
				for(int id : currOp.openingElementIds()) {
					std::shared_ptr<BuildingElement> elem = buildingElements.fromID(id);
					if(elem && isOpeningType(elem->type())) {
						openingElem = elem;
						break;
					}
				}
				if(!openingElem) {
					int id = currOp.openingElementIds().front();
					openingElem = buildingElements.fromID(id);
				}
			}

			searchOpeningSpaceBoundaries(currOp, spaceBoundary, openingElem, convertOptions, openingSpaceBoundaries, *this, searchDist);
		}
	}

	if(!openingSpaceBoundaries.empty()) {
		spaceBoundaries.insert(spaceBoundaries.end(), openingSpaceBoundaries.begin(), openingSpaceBoundaries.end());
	}
}

std::vector<Surface> Space::surfacesOrg() const {
	return m_surfacesOrg;
}

meshVector_t Space::meshSets() const {
	return m_meshSets;
}


static int typeFromElementShape(const shared_ptr<SpaceBoundary>& sb, const objectShapeTypeVector_t& shapes) {
	for(const auto& elemType : shapes) {
		for(const auto& elem : elemType.second) {
			if(sb->guidRelatedElement() == elem->m_entity_guid) {
				return elemType.first;
			}
		}
	}
	return -1;
}

int constructionId(const shared_ptr<SpaceBoundary>& sb, const BuildingElementsCollector& buildingElements) {
	int id = -1;
	std::vector<std::shared_ptr<BuildingElement>> constructionElements = buildingElements.allConstructionElements();
	for(const auto& construction : constructionElements) {
		if(construction->m_guid == sb->guidRelatedElement()) {
			id = construction->m_id;
		}
	}
	if(id == -1) {
		for(const auto& opening : buildingElements.m_openingElements) {
			if(opening->m_guid == sb->guidRelatedElement()) {
				id = opening->m_id;
			}
		}
	}
	return id;
}

void Space::evaluateSpaceBoundaryTypes(const objectShapeTypeVector_t& shapes,
								 const BuildingElementsCollector& buildingElements) {
	for(size_t sbI=0; sbI<m_spaceBoundaries.size(); ++sbI) {
		auto& sb = m_spaceBoundaries[sbI];

		int type = typeFromElementShape(sb, shapes);

		int id = constructionId(sb, buildingElements);

		if(type > -1) {
			sb->setRelatingElementType(static_cast<BuildingElementTypes>(type));
			sb->m_elementEntityId = id;
		}
		else {
			if(sb->isVirtual()) {
				sb->setRelatingElementType(static_cast<BuildingElementTypes>(BET_None));
			}
			else {
				sb->setRelatingElementType(static_cast<BuildingElementTypes>(BET_None));
			}
			sb->m_elementEntityId = -1;
		}

	} // end loop over space boundaries
}

bool Space::evaluateSpaceBoundaryGeometry(shared_ptr<UnitConverter>& unit_converter,
									   std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	if(m_spaceBoundaries.empty())
		return false;


	bool foundOne = false;
	std::vector<int> wrongSBIds;
	for(size_t sbI=0; sbI<m_spaceBoundaries.size(); ++sbI) {
		auto& sb = m_spaceBoundaries[sbI];

		bool res = sb->fetchGeometryFromIFC(unit_converter, m_transformMatrix, errors, convertOptions);
		if(res) {
			foundOne = true;
		}
		else {
			wrongSBIds.push_back(sb->m_id);
		}

	} // end loop over space boundaries

	if(!foundOne) {
		errors.push_back({OT_Space, m_id, "Cannot find at least one connected space boundary"});
	}

	// remove all space boundaries from the list which have non valid surfaces
	if(!wrongSBIds.empty()) {
		for(auto it = m_spaceBoundaries.begin(); it!=m_spaceBoundaries.end();) {
			if(std::find_if(wrongSBIds.begin(), wrongSBIds.end(), [it](int id) -> bool { return id == (*it)->m_id;}) != wrongSBIds.end()) {
				it = m_spaceBoundaries.erase(it);
			}
			else {
				++it;
			}
		}
	}
	return foundOne;
}

bool Space::evaluateSpaceBoundaryFromIFC(const objectShapeTypeVector_t& shapes,
										 const BuildingElementsCollector& buildingElements,
										 shared_ptr<UnitConverter>& unit_converter,
										 std::vector<ConvertError>& errors,
										 const ConvertOptions& convertOptions) {
	// get space boundary types and set element id connections
	evaluateSpaceBoundaryTypes(shapes, buildingElements);

	// convert geometry and create surfaces
	bool res = evaluateSpaceBoundaryGeometry(unit_converter, errors, convertOptions);
	if(!res)
		return false;

	std::vector<std::shared_ptr<SpaceBoundary>> splittedSBs;
	for(auto sb : m_spaceBoundaries) {
		std::vector<std::shared_ptr<SpaceBoundary>> ssbs = sb->splitBySurfaces();
		if(!ssbs.empty()) {
			splittedSBs.insert(splittedSBs.end(), ssbs.begin(), ssbs.end());
		}
	}
	if(!splittedSBs.empty()) {
		m_spaceBoundaries.insert(m_spaceBoundaries.end(), splittedSBs.begin(), splittedSBs.end());
	}

	int wrongSurfaces = 0;
	for(auto sb : m_spaceBoundaries) {
		if(!sb->checkAndHealSurface(true, convertOptions.m_polygonEps)) {
			++wrongSurfaces;
		}
	}
	if(wrongSurfaces > 0) {
		errors.push_back(ConvertError{OT_Space, m_id, "Space contains " + std::to_string(wrongSurfaces) + " space boundaries with non valid surface."});
	}

	// create two temporary vectors for construction space boundaries and opening space boundaries
	std::vector<std::shared_ptr<SpaceBoundary>> constructionSBs;
	std::vector<std::shared_ptr<SpaceBoundary>> openingSBs;
	for(auto sb : m_spaceBoundaries) {
		if(sb->isConstructionElement() || sb->isVirtual())
			constructionSBs.push_back(sb);
		if(sb->isOpeningElement())
			openingSBs.push_back(sb);
	}

	if(openingSBs.empty())
		return true;

//	std::map<int,std::vector<int>> parallelOpeningSBs;
//	for(size_t ci=0; ci<constructionSBs.size(); ++ci) {
//		const Surface& constrSurf = constructionSBs[ci]->surface();
//		for(size_t oi=0; oi<openingSBs.size(); ++oi) {
//			const Surface& openingSurf = openingSBs[oi]->surface();
//			if(constrSurf.isParallelTo(openingSurf))
//				parallelOpeningSBs[ci].push_back(oi);
//		}
//	}

	// try to find out which opening sb is related to which construction sb
	std::vector<int> addedOpeningIds;
	for(auto openingSB : openingSBs) {
		// check if the opening sb is already added
		int id = openingSB->m_id;

		const Surface& opSurf = openingSB->surface();
		int notParallel = 0;
		int wrongDistance = 0;
		int notIntersected = 0;
		for(auto constrSB : constructionSBs ) {
			const Surface& constrSurf = constrSB->surface();

			if(std::find_if(addedOpeningIds.begin(),addedOpeningIds.end(), [id](int addedId) -> bool { return id == addedId; }) != addedOpeningIds.end())
				continue;

			// fetch thickness of construction element if exist
			int elementId = constrSB->m_elementEntityId;
			std::shared_ptr<BuildingElement> element = buildingElements.fromID(elementId);
			double searchDist = convertOptions.m_openingDistance;
			if(element)
				searchDist = std::max(element->thickness(),searchDist);

			// find subsurfaces in surfaces which have already cutted openings
			// in this case surface and subsurface must have same points
			std::vector<std::pair<size_t,size_t>> samepoints = constrSurf.samePoints(opSurf, convertOptions.m_distanceEps);
			if(samepoints.size() > 2) {
				// heal the construction surface by merging with subsurface
				constrSB->mergeSurface(opSurf);
				constrSB->addContainedOpeningSpaceBoundaries(openingSB);
				addedOpeningIds.push_back(openingSB->m_id);
				continue;
			}
			// check for parallel and intersected surfaces
			else {
				if(constrSurf.isParallelTo(opSurf, convertOptions.m_distanceEps)) {
					double dist = constrSurf.distanceToParallelPlane(opSurf, convertOptions.m_distanceEps);
					bool isIntersected = constrSurf.isIntersected(opSurf);
					if(dist <= searchDist*1.1 && isIntersected) {
						constrSB->addContainedOpeningSpaceBoundaries(openingSB);
						addedOpeningIds.push_back(openingSB->m_id);
						continue;
					}
					else {
						if(dist > searchDist*1.1) {
							++wrongDistance;
						}
						else if(!isIntersected) {
							++notIntersected;
						}
					}
				}
				else {
					++notParallel;
				}
			}
		}
		// check if and why the opening isn't found
		int constructionCount = constructionSBs.size();
		int failures = notParallel + wrongDistance + notIntersected;
		std::string failureReasonString;
		if(notParallel > 0)
			failureReasonString += "NP: " + std::to_string(notParallel) +  " : ";
		if(wrongDistance > 0)
			failureReasonString += "WD: " + std::to_string(wrongDistance) +  " : ";
		if(notIntersected > 0)
			failureReasonString += "NI: " + std::to_string(notIntersected);
		if(failures == constructionCount) {
			errors.push_back(ConvertError{OT_Space, m_id, "Opening space boundary id '" + std::to_string(openingSB->m_id) + "' has no connection because " + failureReasonString});
		}

	}

	std::vector<std::shared_ptr<SpaceBoundary>> missingOpeningSBs;
	for(auto openingSB : openingSBs) {
		int id = openingSB->m_id;
		if(std::find_if(addedOpeningIds.begin(),addedOpeningIds.end(), [id](int addedId) -> bool { return id == addedId; }) == addedOpeningIds.end()) {
			missingOpeningSBs.push_back(openingSB);
		}
	}

	if(missingOpeningSBs.size() > 0) {
		for(auto sb : missingOpeningSBs) {
			std::map<int,int> distIndexMap;
			errors.push_back(ConvertError{OT_Space, m_id, "Opening space boundary id '" + std::to_string(sb->m_ifcId) + "' is not connected to a construction space boundary"});
//			for(size_t ci=0; ci<constructionSBs.size(); ++ci) {
//				const Surface& constrSurf = constructionSBs[ci]->surface();
//				if(constrSurf.isParallelTo(sb->surface()) && constrSurf.isIntersected(sb->surface())) {
//					double dist = constrSurf.distanceToParallelPlane(sb->surface());
//					distIndexMap[int(dist*10000)] = ci;
//				}
//			}
//			if(!distIndexMap.empty()) {
//				double dist = distIndexMap.begin()->first / 10000.0;
//				int smallestIndex = distIndexMap.begin()->second;
//			}
		}
	}

	return true;
}


std::vector<std::shared_ptr<SpaceBoundary>> Space::createConstructionSpaceBoundaries(
	const BuildingElementsCollector& buildingElements,
	std::vector<ConvertError>& errors,
	const ConvertOptions& convertOptions) {
	return createSpaceBoundaries_2(buildingElements, errors, convertOptions);
}

bool Space::finalizeConstructionSpaceBoundaries(
	std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
	const BuildingElementsCollector& buildingElements,
	std::vector<Opening>& openings,
	std::vector<ConvertError>& errors,
	const ConvertOptions& convertOptions) {
	createSpaceBoundariesForOpeningsFromSpaceBoundaries(spaceBoundaries, buildingElements, openings, errors, convertOptions);
	m_spaceBoundaries = spaceBoundaries;
	if(m_spaceBoundaries.empty()) {
		errors.push_back({OT_Space, m_id, "Cannot evaluate any space boundary for this space"});
		return false;
	}
	return true;
}

bool Space::evaluateSpaceBoundariesFromConstruction(const BuildingElementsCollector& buildingElements, std::vector<Opening>& openings,
									std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	std::vector<ConvertError> phase1Errors;
	auto sbs = createConstructionSpaceBoundaries(buildingElements, phase1Errors, convertOptions);
	errors.insert(errors.end(), phase1Errors.begin(), phase1Errors.end());
	return finalizeConstructionSpaceBoundaries(sbs, buildingElements, openings, errors, convertOptions);
}

bool Space::updateSpaceBoundaries(const objectShapeTypeVector_t& shapes,
								  shared_ptr<UnitConverter>& unit_converter,
								  const BuildingElementsCollector& buildingElements,
								  std::vector<Opening>& openings,
								  bool useSpaceBoundaries,
								  std::vector<ConvertError>& errors,
								  const ConvertOptions& convertOptions) {

	bool success;
	// update existing space boundaries from IFC
	if(useSpaceBoundaries && !m_spaceBoundaries.empty()) {
		// get space boundary types and set element id connections
		// convert geometry and create surfaces
		success = evaluateSpaceBoundaryFromIFC(shapes, buildingElements, unit_converter, errors, convertOptions);
	}
	// try to evaluate space boundaries from building element entities
	else {
		success = evaluateSpaceBoundariesFromConstruction(buildingElements, openings, errors, convertOptions);
	}
	return success;
}

const std::vector<std::shared_ptr<SpaceBoundary>>& Space::spaceBoundaries() const {
	return m_spaceBoundaries;
}

void Space::removeDublicatedSpaceBoundaries(const ConvertOptions& convertOptions) {
	if(m_spaceBoundaries.size() < 2)
		return;

	std::set<size_t> indicesToRemove;
	for(size_t i=0; i<m_spaceBoundaries.size()-1; ++i) {
		auto sb1 = m_spaceBoundaries[i];
		for(size_t j=i+1; j<m_spaceBoundaries.size(); ++j) {
			auto sb2 = m_spaceBoundaries[j];
			if(sb1->surface().isEqualTo(sb2->surface(), convertOptions.m_distanceEps))
				indicesToRemove.insert(j);
		}
	}
	if(!indicesToRemove.empty()) {
		std::vector<std::shared_ptr<SpaceBoundary>> tempSbs;
		for(size_t i=0; i<m_spaceBoundaries.size(); ++i) {
			if(indicesToRemove.count(i) == 0)
				tempSbs.push_back(m_spaceBoundaries[i]);
		}
		m_spaceBoundaries = tempSbs;
	}
}

std::vector<int> Space::checkUniqueSubSurfaces() const {
	std::vector<int> res;
	std::set<int> usedSubSurfaceIds;
	for(auto sb : m_spaceBoundaries) {
		for(auto subsb : sb->containedOpeningSpaceBoundaries()) {
			if(usedSubSurfaceIds.count(subsb->surface().id()) > 0) {
				res.push_back(subsb->m_id);
			}
			else {
				usedSubSurfaceIds.insert(subsb->surface().id());
			}
		}
	}
	return res;
}

void Space::checkForEqualSpaceBoundaries(std::vector<std::pair<int,int>>& equalSBs, const ConvertOptions& convertOptions) const {
	if(m_spaceBoundaries.size() < 2)
		return;

	for(size_t i=0; i<m_spaceBoundaries.size()-1; ++i) {
		auto sb1 = m_spaceBoundaries[i];
		for(size_t j=i+1; j<m_spaceBoundaries.size(); ++j) {
			auto sb2 = m_spaceBoundaries[j];
			if(sb1->surface().isEqualTo(sb2->surface(), convertOptions.m_distanceEps))
				equalSBs.push_back({sb1->m_id, sb2->m_id});
		}
	}
}

bool Space::isIntersected(const Space& other, const ConvertOptions& convertOptions) const {
	for(const auto& p1 : m_meshSets) {
		for(const auto& p2 : other.m_meshSets) {
			if(IFCC::isIntersected(p1.get(), p2.get()))
				return true;
		}
	}
	for(const auto& p1 : m_spaceBoundaries) {
		for(const auto& p2 : other.m_spaceBoundaries) {
			const Surface & surf1 = p1->surface();
			const Surface & surf2 = p2->surface().polygon();
			if(surf1.isValid(convertOptions.m_polygonEps) && surf2.isValid(convertOptions.m_polygonEps)) {
				if(IBKMK::polyIntersect(surf1.polygon(), surf2.polygon()))
					return true;
			}
		}
	}
	return false;
}

VICUS::Room Space::getVicusObject(const ConvertOptions& options) const {
	VICUS::Room vroom;
	vroom.m_id = m_id;
	if(!m_longName.empty())
		vroom.m_displayName = QString::fromStdString(m_longName + "_" + std::to_string(m_ifcId));
	else if(!m_name.empty())
		vroom.m_displayName = QString::fromStdString(m_name + "_" + std::to_string(m_ifcId));
	vroom.m_ifcGUID = m_guid;

	std::vector<VICUS::Surface> surfaces;
	for(const auto& sb : m_spaceBoundaries) {
		VICUS::Surface vsurf = sb->getVicusSurface(options);
		if(vsurf.m_id != INVALID_ID)
			surfaces.push_back(vsurf);
	}
	if(!surfaces.empty())
		vroom.setSurfaces(surfaces);

	return vroom;
}

TiXmlElement * Space::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Room");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_longName.empty())
		e->SetAttribute("displayName", m_longName + "_" + std::to_string(m_ifcId));
	else if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_ifcId));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	bool sbsReady = false;
	for(auto sb : m_spaceBoundaries) {
		if(sb->checkSurface(convertOptions.m_polygonEps))
			sbsReady = true;
	}
	if(sbsReady) {
		TiXmlElement * child = new TiXmlElement("Surfaces");
		e->LinkEndChild(child);
		for(auto sb : m_spaceBoundaries) {
			sb->writeXML(child, convertOptions);
		}
	}
	return e;
}

bool Space::hasSpaceBoundary(const std::string &guid) const {
	for(const std::string& currguid : m_spaceBoundaryGUIDs) {
		if(currguid == guid)
			return true;
	}
	return false;
}

bool Space::shareSameSpaceBoundary(const Space &space) const {
	for(const std::string& currguid : m_spaceBoundaryGUIDs) {
		if(space.hasSpaceBoundary(currguid))
			return true;
	}
	return false;
}


} // namespace IFCC
