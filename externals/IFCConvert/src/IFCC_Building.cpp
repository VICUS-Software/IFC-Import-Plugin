#include "IFCC_Building.h"

#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcBuilding.h>
#include <ifcpp/IFC4X3/include/IfcSpatialStructureElement.h>


#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_ProgressHandler.h"
#include "IFCC_Space.h"
#include "IFCC_BuildingStorey.h"
#include "IFCC_Logger.h"

namespace IFCC {

/*! Opening id selected via IFCC_DEBUG_OPENING_ID for unthrottled match logging. */
static int debugOpeningId() {
	static int id = [](){
		const char* e = std::getenv("IFCC_DEBUG_OPENING_ID");
		return e ? std::atoi(e) : -1;
	}();
	return id;
}

/*! Unnormalized Newell normal of a polygon (orientation-sensitive). */
static IBKMK::Vector3D newellNormal(const polygon3D_t& poly) {
	IBKMK::Vector3D n(0,0,0);
	const size_t cnt = poly.size();
	for(size_t i=0; i<cnt; ++i) {
		const IBKMK::Vector3D& a = poly[i];
		const IBKMK::Vector3D& b = poly[(i+1)%cnt];
		n.m_x += (a.m_y - b.m_y) * (a.m_z + b.m_z);
		n.m_y += (a.m_z - b.m_z) * (a.m_x + b.m_x);
		n.m_z += (a.m_x - b.m_x) * (a.m_y + b.m_y);
	}
	return n;
}

Building::Building(int id) :
	EntityBase(id)
{}

bool Building::set(std::shared_ptr<IFC4X3::IfcSpatialStructureElement> ifcElement) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcElement)))
		return false;

	const std::vector<weak_ptr<IFC4X3::IfcRelAggregates> >& vec_decomposedBy = ifcElement->m_IsDecomposedBy_inverse;
	for(const auto& contentElement : vec_decomposedBy) {
		if( contentElement.expired() ) {
			continue;
		}
		shared_ptr<IFC4X3::IfcRelAggregates> rel_aggregates( contentElement );
		if( rel_aggregates ) {
			const std::vector<shared_ptr<IFC4X3::IfcObjectDefinition> >& vec_related_objects = rel_aggregates->m_RelatedObjects;
			for(const auto& contObj : vec_related_objects) {
				if( contObj ) {
					shared_ptr<IFC4X3::IfcBuildingStorey> storey = std::dynamic_pointer_cast<IFC4X3::IfcBuildingStorey>(contObj);
					if(storey != nullptr)
						m_storeysOriginal.push_back(storey);

					shared_ptr<IFC4X3::IfcSpace> space = std::dynamic_pointer_cast<IFC4X3::IfcSpace>(contObj);
					if(space != nullptr)
						m_spacesOriginal.push_back(space);
				}
			}
		}
	}

	return true;
}

void Building::fetchStoreys(const objectShapeGUIDMap_t& storeys, const objectShapeGUIDMap_t& spaces, bool onlyOne) {
	if(storeys.empty()) {
		std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
		if(m_spacesOriginal.empty()) {
			if(storey->set(spaces)) {
				storey->m_name = "Only one storey";
				m_storeys.push_back(storey);
			}
		}
		else {
			if(storey->set(m_spacesOriginal)) {
				storey->m_name = "Only one storey";
				m_storeys.push_back(storey);
			}
		}
	}
	else {
		for(const auto& shape : storeys) {
			if(!m_storeysOriginal.empty()) {
				for(const auto& opOrg : m_storeysOriginal) {
					std::string guid = guidFromObject(opOrg.get());
					if(shape.first == guid) {
						std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
						if(storey->set(opOrg)) {
							m_storeys.push_back(storey);
						}
						break;
					}
				}
			}
			else {
				if(onlyOne) {
					std::shared_ptr<BuildingStorey> storey = std::shared_ptr<BuildingStorey>(new BuildingStorey(GUID_maker::instance().guid()));
					std::shared_ptr<IFC4X3::IfcSpatialStructureElement> se = std::dynamic_pointer_cast<IFC4X3::IfcSpatialStructureElement>(shape.second->m_ifc_object_definition.lock());
					if(se != nullptr) {
						storey->set(se);
						m_storeys.push_back(storey);
					}
				}
			}
		}
	}
}

bool Building::updateStoreys(const objectShapeTypeVector_t& elementShapes,
							 const objectShapeGUIDMap_t& spaceShapes,
							 shared_ptr<UnitConverter>& unit_converter,
							 const BuildingElementsCollector& buildingElements,
							 std::vector<Opening>& openings,
							 bool useSpaceBoundaries,
							 std::vector<ConvertError>& errors,
							 const ConvertOptions& convertOptions,
							 IBK::NotificationHandler* notify) {

	if(m_storeys.empty()) {
		errors.push_back(ConvertError{OT_Building, m_id, "Building id '" + std::to_string(m_ifcId) + "' has no storeys"});
		return false;
	}
	size_t n = m_storeys.size();
	for(size_t si = 0; si < n; ++si) {
		// Give each storey its own sub-range so progress never jumps backwards
		double rangeStart = double(si) / double(n);
		double rangeEnd   = double(si + 1) / double(n);
		std::string label = "Storey " + std::to_string(si+1) + "/" + std::to_string(n);

		if(notify)
			notify->notify(rangeStart, label.c_str());

		m_storeys[si]->fetchSpaces(spaceShapes, unit_converter, errors);

		if(notify) {
			ProgressHandler storeyHandler([notify](int v, QString t) {
				// Keep the QByteArray alive for the duration of the notify call —
				// otherwise its backing buffer is freed before notify reads from it.
				QByteArray utf8 = t.toUtf8();
				const char* text = t.isEmpty() ? nullptr : utf8.constData();
				notify->notify(double(v) / 100.0, text);
			}, rangeStart, rangeEnd);
			m_storeys[si]->updateSpaces(elementShapes, unit_converter, buildingElements,
										openings, useSpaceBoundaries, errors, convertOptions, &storeyHandler);
		}
		else {
			m_storeys[si]->updateSpaces(elementShapes, unit_converter, buildingElements,
										openings, useSpaceBoundaries, errors, convertOptions, nullptr);
		}
	}

	// Cross-space fallback. Per-space matching sees only its own SBs and commits on
	// real geometric intersection. Openings whose geometry is in a wall shared across
	// rooms (curtainwalls) may miss every room's SB slice, or their IFC relationship
	// may point to a wall whose SB's polygon doesn't contain the opening. This global
	// sweep considers every construction SB in the building.
	//
	// Pass priority (most-trusted first):
	//   Pass A: strict intersect AND respect IfcRelVoidsElement (host wall lists
	//           the opening in m_containedOpenings). Trusted IFC topology — the
	//           best signal for which room hosts the opening.
	//   Pass B: strict intersect, ignore containedOpenings filter — reaches
	//           IFCs with broken/missing IfcRelVoidsElement.
	//   Pass C: coplanar-accept fallback for curtain-wall scenarios where the
	//           room's SB is a partial slice of the full wall face.
	size_t matchedStrict = 0, matchedTopology = 0, matchedCoplanar = 0, matchedMultiSpace = 0;
	// A pass-A/B winner whose element sits within this distance of the matched patch
	// is trusted outright; above it the later (less trusted) passes still get a
	// chance to produce a CLOSER candidate — WSHH-style room-spanning opening boxes
	// produce large phantom intersections on the topology-registered wall while the
	// true wall only matches via the unfiltered/straddle paths.
	const double kTrustedElemDist = 2.0;
	// A winner that captures less than half the element area is equally suspicious —
	// keep evaluating later passes so a full-size match can outrank the sliver.
	auto suspicious = [kTrustedElemDist](const Space::OpeningMatchCandidate& b) -> bool {
		if(!b.parentSB)
			return true;
		if(b.dist > kTrustedElemDist)
			return true;
		if(b.openingElem) {
			double elemArea = b.openingElem->openingArea();
			if(elemArea > 0.1 && b.area < 0.5 * elemArea)
				return true;
		}
		return false;
	};
	// Debug/eval kill-switch: IFCC_NO_MULTISPACE=1 restores the pre-multi-space
	// behavior (openings with any SB skipped, single best-space commit only).
	const bool noMultiSpace = (std::getenv("IFCC_NO_MULTISPACE") != nullptr);
	// Per-space best candidate for one opening — openings can span several spaces
	// (interior doors/windows bound TWO rooms, shaft openings even more), so the
	// sweep keeps one candidate per space instead of a single global winner.
	struct SpaceCandidate {
		std::shared_ptr<Space>			space;
		Space::OpeningMatchCandidate	cand;
		bool							fromCoplanar = false;
		bool							ignoredFilter = false;
		size_t*							counter = nullptr;
	};
	for(Opening& op : openings) {
		// Openings already linked per-space keep those links; the sweep below only
		// considers spaces WITHOUT a link yet (an interior door matched in room A but
		// deferred/failed in room B must still get its room-B side). For such openings
		// only the trusted topology pass runs — they are not orphans, so the loose
		// B/C rescue passes don't apply.
		const bool hadSB = op.hasSpaceBoundary();
		if(noMultiSpace && hadSB)
			continue;

		std::vector<SpaceCandidate> perSpace;

		auto runPass = [&](bool ignoreContainedOpeningsFilter, bool allowCoplanarAccept, size_t* counter) {
			for(const auto& storey : m_storeys) {
				for(const auto& space : storey->spaces()) {
					if(op.hasSpaceBoundaryInSpace(space->m_guid))
						continue;
					Space::OpeningMatchCandidate c = space->findBestOpeningMatch(op, buildingElements, convertOptions,
						ignoreContainedOpeningsFilter, allowCoplanarAccept);
					if(!c.parentSB)
						continue;
					auto it = std::find_if(perSpace.begin(), perSpace.end(),
										   [&space](const SpaceCandidate& sc) -> bool { return sc.space == space; });
					if(it == perSpace.end())
						perSpace.push_back(SpaceCandidate{space, c, allowCoplanarAccept, ignoreContainedOpeningsFilter, counter});
					else if(Space::isBetterOpeningMatch(c, it->cand)) {
						it->cand = c;
						it->fromCoplanar = allowCoplanarAccept;
						it->ignoredFilter = ignoreContainedOpeningsFilter;
						it->counter = counter;
					}
				}
			}
		};
		auto globalBest = [&perSpace]() -> SpaceCandidate* {
			SpaceCandidate* b = nullptr;
			for(auto& sc : perSpace)
				if(b == nullptr || Space::isBetterOpeningMatch(sc.cand, b->cand))
					b = &sc;
			return b;
		};

		// Pass A — strict intersect, IFC topology required.
		runPass(/*ignoreContainedOpeningsFilter=*/false, /*allowCoplanarAccept=*/false, &matchedTopology);
		SpaceCandidate* best = globalBest();

		if(!hadSB) {
			// Pass B — strict intersect, no topology filter. Entered when pass A found
			// nothing OR its winner is suspicious (far from the element / sliver-sized).
			if(best == nullptr || suspicious(best->cand)) {
				runPass(/*ignoreContainedOpeningsFilter=*/true, /*allowCoplanarAccept=*/false, &matchedStrict);
				best = globalBest();
			}
			// Pass C — coplanar-accept fallback.
			if(best == nullptr || suspicious(best->cand)) {
				runPass(/*ignoreContainedOpeningsFilter=*/true, /*allowCoplanarAccept=*/true, &matchedCoplanar);
				best = globalBest();
			}
		}
		if(best == nullptr)
			continue;

		const Space::OpeningMatchCandidate& bc = best->cand;
		double elemArea = bc.openingElem ? bc.openingElem->openingArea() : 0.0;
		bool primaryCommitted = false;
		if(!hadSB && bc.area > 0.0) {
			// Coverage sanity: if even the building-wide best candidate captures less
			// than a third of the window/door area, every reachable surface merely
			// grazes the opening (WSHH: 10m opening boxes passing through rooms whose
			// facade SB has a hole where the window belongs). Committing the sliver
			// glues the window onto an unrelated wall fragment — worse than leaving
			// the opening unmatched and reporting it.
			if(elemArea > 0.1 && bc.area < 0.30 * elemArea) {
				Logger::instance() << "Building::updateStoreys: SKIP sliver match opening id=" << op.m_id
								   << " name='" << op.m_name << "' bestArea=" << bc.area
								   << " elemArea=" << elemArea
								   << " sb='" << bc.parentSB->m_name << "'";
			}
			else {
				// The opening frequently spans several coplanar wall fragments of the
				// winning space — commit ALL split pieces, not just the best fragment
				// (a partial hole loses the rest of the window, user report).
				std::vector<Space::OpeningMatchCandidate> allCands;
				best->space->findBestOpeningMatch(op, buildingElements, convertOptions,
												  best->ignoredFilter, best->fromCoplanar, &allCands);
				std::vector<Space::OpeningMatchCandidate> pieces =
						Space::collectSplitPieces(bc, allCands, convertOptions);
				if(pieces.empty())
					pieces.push_back(bc);
				for(size_t pi=0; pi<pieces.size(); ++pi) {
					best->space->commitOpeningMatch(op, pieces[pi], convertOptions);
					if(pi > 0) {
						Logger::instance() << "Building::updateStoreys: SPLIT cross-commit opening id=" << op.m_id
										   << " name='" << op.m_name << "' -> sb='" << pieces[pi].parentSB->m_name
										   << "' area=" << pieces[pi].area;
					}
				}
				if(best->counter)
					++(*best->counter);
				primaryCommitted = true;
			}
		}

		// Multi-space commits: attach the opening in every FURTHER space it
		// geometrically bounds. Gated much stricter than the primary commit — a
		// second room side must look like a full-quality match on its own (strict
		// intersect, near the element, covering the element) so WSHH-style phantom
		// intersections in passed-through rooms don't gain extra attachments.
		//
		// Opposite-side gate: a genuine further room side lies on the other face of
		// the SAME wall — a near-copy of an existing attachment (parallel plane,
		// centroid within the opening body depth). Phantom intersections of oversized
		// opening boxes sit meters away laterally (next room along the facade) or on
		// perpendicular partitions, and fail one of the two checks.
		const double kMaxSideSeparation = 1.2;   // [m] wall assembly depth + clip-offset slack
		const double kMinSideParallel   = 0.7;   // |cos| between side normals
		// Complement pieces (window part in the NEIGHBOR space) frequently sit on a
		// DIFFERENT wall-layer plane than the primary side — WSHH stairwell windows
		// straddle two storeys whose facade SBs are 26cm apart ('Gipsputz - 2' vs
		// 'Gipsputz - 4'). Allow this much plane offset for the complement test;
		// interior partitions parallel to the facade sit further back and stay out.
		const double kMaxComplementPlaneOffset = 0.4;   // [m]
		// NOTE: the primary commit already registered its SB(s) with the opening, so
		// op.spaceBoundaries() covers them — do NOT add bc.mergedSurface again
		// (double-counting inflated the complement area cap and silently dropped
		// the neighbor-space pieces of storey-straddling stairwell windows).
		std::vector<Surface> committedSides;
		for(const auto& sb : op.spaceBoundaries()) {
			if(sb)
				committedSides.push_back(sb->surface());
		}
		// Relation of a further-space candidate to the already committed sides:
		//  SR_None       — unrelated patch (phantom) -> skip
		//  SR_Opposite   — other face of the same wall (interior door second side)
		//  SR_Complement — SAME plane, non-overlapping: the window part in a
		//                  NEIGHBOR space. Stairwell windows sit at half-storey
		//                  height and straddle two storey spaces — each space's
		//                  facade SB covers only a fraction, so the per-piece
		//                  coverage gates must not apply (the pieces jointly
		//                  cover the window).
		//  SR_Duplicate  — same plane, overlapping an existing side -> skip
		enum SideRelation { SR_None, SR_Opposite, SR_Complement, SR_Duplicate };
		auto sideRelation = [&](const Surface& cand, double candArea) -> SideRelation {
			IBKMK::Vector3D nc = newellNormal(cand.polygon());
			double ncLen = nc.magnitude();
			if(ncLen < 1e-10)
				return SR_None;
			nc *= 1.0/ncLen;
			const double dc = nc.scalarProduct(cand.centroid());
			SideRelation rel = SR_None;
			bool overlapsCommitted = false;
			for(const Surface& ref : committedSides) {
				IBKMK::Vector3D nr = newellNormal(ref.polygon());
				double nrLen = nr.magnitude();
				if(nrLen < 1e-10)
					continue;
				nr *= 1.0/nrLen;
				double cosAngle = std::fabs(nc.scalarProduct(nr));
				if(cosAngle < kMinSideParallel)
					continue;
				double dr = nr.scalarProduct(ref.centroid());
				if(nc.scalarProduct(nr) < 0) dr = -dr;
				if(cosAngle >= 0.99 && std::fabs(dc - dr) <= kMaxComplementPlaneOffset) {
					// Parallel plane within the wall assembly: complement, opposite face
					// or duplicate — decided by the overlap of the PROJECTED regions.
					// Offset planes must be projected first, Surface::intersect needs
					// coplanar polygons.
					Surface projCand = cand;
					if(std::fabs(dc - dr) > 0.05) {
						polygon3D_t moved = cand.polygon();
						const IBKMK::Vector3D& p0 = ref.centroid();
						for(IBKMK::Vector3D& v : moved) {
							double d = nr.m_x*(v.m_x-p0.m_x) + nr.m_y*(v.m_y-p0.m_y) + nr.m_z*(v.m_z-p0.m_z);
							v.m_x -= nr.m_x * d;
							v.m_y -= nr.m_y * d;
							v.m_z -= nr.m_z * d;
						}
						projCand = Surface(moved);
					}
					Surface inter = ref.intersect(projCand);
					double ia = inter.isValid(1e-3) ? inter.area() : 0.0;
					if(ia > 0.2 * candArea) {
						if(std::fabs(dc - dr) <= 0.05)
							return SR_Duplicate;
						// same window region seen from the other wall face — leave it to
						// the strictly gated opposite-side path below
						overlapsCommitted = true;
						continue;
					}
					rel = SR_Complement;
					continue;
				}
				double dist = (cand.centroid() - ref.centroid()).magnitude();
				if(dist <= kMaxSideSeparation && rel == SR_None)
					rel = SR_Opposite;
			}
			if(overlapsCommitted)
				return SR_Opposite;
			return rel;
		};
		// Trust anchor: mirroring a wrong primary onto the wall's other face doubles the
		// damage (WSHH box openings glued to an interior partition get a second copy in
		// the room behind it). Openings without any per-space attachment only receive
		// further-space commits when the fallback primary itself is trustworthy — the
		// element sits close to the committed patch.
		const bool anchorTrusted = hadSB || (primaryCommitted && bc.dist <= kTrustedElemDist);
		for(SpaceCandidate& sc : perSpace) {
			if(noMultiSpace || !anchorTrusted)
				break;
			if(primaryCommitted && &sc == best)
				continue;
			if(sc.fromCoplanar)
				continue;
			if(!sc.cand.parentSB || sc.cand.area <= 0.0)
				continue;
			// Must be the opposite face of an already-attached side — otherwise it's a
			// lateral/perpendicular phantom intersection of an oversized opening body.
			// Complement pieces (window part in the neighbor space, SAME plane) are
			// often hidden behind the space's best candidate — search the space's
			// FULL candidate list for them first.
			{
				std::vector<Space::OpeningMatchCandidate> allCands;
				sc.space->findBestOpeningMatch(op, buildingElements, convertOptions,
											   sc.ignoredFilter, sc.fromCoplanar, &allCands);
				for(const Space::OpeningMatchCandidate& cc : allCands) {
					if(!cc.parentSB || cc.area < 0.05)
						continue;
					SideRelation ccRel = sideRelation(cc.mergedSurface, cc.area);
					if(op.m_id == debugOpeningId())
						Logger::instance() << "  dbg-open: complement-check space='" << sc.space->m_name
										   << "' sb='" << cc.parentSB->m_name << "' area=" << cc.area
										   << " rel=" << int(ccRel);
					if(ccRel != SR_Complement)
						continue;
					double ccElemArea = cc.openingElem ? cc.openingElem->openingArea() : 0.0;
					double committedArea = 0.0;
					for(const Surface& ref : committedSides)
						committedArea += ref.area();
					if(op.m_id == debugOpeningId())
						Logger::instance() << "  dbg-open: complement-cap committed=" << committedArea
										   << " cc=" << cc.area << " elem=" << ccElemArea
										   << " sides=" << committedSides.size();
					if(ccElemArea > 0.1 && committedArea + cc.area > 1.25 * ccElemArea)
						continue;
					sc.space->commitOpeningMatch(op, cc, convertOptions);
					committedSides.push_back(cc.mergedSurface);
					++matchedMultiSpace;
					Logger::instance() << "Building::updateStoreys: MULTI-SPACE commit opening id=" << op.m_id
									   << " name='" << op.m_name << "' space='" << sc.space->m_name << "'"
									   << " sb='" << cc.parentSB->m_name << "' area=" << cc.area
									   << " rel=complement";
				}
			}
			const SideRelation rel = sideRelation(sc.cand.mergedSurface, sc.cand.area);
			if(rel != SR_Opposite) {
				if(op.m_id == debugOpeningId())
					Logger::instance() << "  dbg-open: MULTI-SPACE reject (rel=" << int(rel) << ") space='"
									   << sc.space->m_name << "' sb='" << sc.cand.parentSB->m_name << "'";
				continue;
			}
			double scElemArea = sc.cand.openingElem ? sc.cand.openingElem->openingArea() : 0.0;
			const bool hasAreaSignal = scElemArea > 0.1;
			const bool hasDistSignal = sc.cand.dist < 1e19;
			{
				// Opposite wall face: must look like a full-quality match on its own.
				// Without any quality signal (no element geometry AND no position) a
				// further-space commit is guesswork — leave it to the primary only.
				if(!hasAreaSignal && !hasDistSignal)
					continue;
				if(hasAreaSignal && sc.cand.area < 0.5 * scElemArea)
					continue;
				if(sc.cand.area < 0.5 * bc.area)
					continue;
				if(hasDistSignal) {
					// Allow slightly beyond the trusted distance when even the best match
					// sits far out (placement origin at a corner of the opening box).
					double distCap = std::max(kTrustedElemDist, bc.dist < 1e19 ? 1.25 * bc.dist : kTrustedElemDist);
					if(sc.cand.dist > distCap)
						continue;
				}
			}
			sc.space->commitOpeningMatch(op, sc.cand, convertOptions);
			committedSides.push_back(sc.cand.mergedSurface);
			++matchedMultiSpace;
			Logger::instance() << "Building::updateStoreys: MULTI-SPACE commit opening id=" << op.m_id
							   << " name='" << op.m_name << "' space='" << sc.space->m_name << "'"
							   << " sb='" << sc.cand.parentSB->m_name << "' area=" << sc.cand.area
							   << " dist=" << sc.cand.dist << " rel=" << (rel == SR_Complement ? "complement" : "opposite");
		}
	}
	Logger::instance() << "Building::updateStoreys: cross-space fallback matched "
					   << matchedTopology << " topology + " << matchedStrict << " strict + "
					   << matchedCoplanar << " coplanar + " << matchedMultiSpace
					   << " multi-space of " << openings.size() << " openings";

	// Full-outline expansion, LAST resort after all per-space/complement commits:
	// a single-part opening hosted on a synthetic 'Missing' fill that still covers
	// clearly less than the element outline has its remainder OUTSIDE every space
	// boundary (WSHH stairwell arch windows against the storey-slab band that
	// belongs to no room) — no complement can ever attach it. Grow the synthetic
	// fill together with the hole; real construction hosts stay untouched.
	// Runs after the multi-space pass so genuine neighbor-space complements always
	// win over the expansion.
	size_t expandedFills = 0;
	for(Opening& op : openings) {
		if(op.spaceBoundaries().size() != 1)
			continue;
		const std::shared_ptr<SpaceBoundary>& opSB = op.spaceBoundaries().front();
		if(!opSB)
			continue;
		for(const auto& storey : m_storeys) {
			bool done = false;
			for(const auto& space : storey->spaces()) {
				if(space->expandMissingHostToOpeningOutline(op, opSB, buildingElements, convertOptions)) {
					++expandedFills;
					done = true;
					break;
				}
			}
			if(done)
				break;
		}
	}
	if(expandedFills > 0)
		Logger::instance() << "Building::updateStoreys: expanded " << expandedFills
						   << " Missing-fill hosts to full opening outlines";

	// Orphan rescue: openings without ANY space boundary whose hole is carved out
	// of the space shell (empty IfcOpeningElement duct/stair openings) — no
	// surface exists at the hole position, so all matching passes come up empty.
	// Attach them as zero-depth flaps on the Missing fill they border.
	size_t orphanFlaps = 0;
	for(Opening& op : openings) {
		if(op.hasSpaceBoundary())
			continue;
		for(const auto& storey : m_storeys) {
			bool done = false;
			for(const auto& space : storey->spaces()) {
				if(space->attachOrphanOpeningFlap(op, buildingElements, convertOptions)) {
					++orphanFlaps;
					done = true;
					break;
				}
			}
			if(done)
				break;
		}
	}
	if(orphanFlaps > 0)
		Logger::instance() << "Building::updateStoreys: attached " << orphanFlaps
						   << " orphan openings as Missing-fill flaps";

	// Optional full map opening guid -> id/state for tracing specific windows
	// reported from the GUI (env IFCC_LOG_OPENING_MAP=1).
	if(std::getenv("IFCC_LOG_OPENING_MAP") != nullptr) {
		for(const Opening& op : openings) {
			Logger::instance() << "opening-map id=" << op.m_id << " guid=" << op.guid()
							   << " name='" << op.m_name << "' sbCount=" << op.spaceBoundaries().size();
		}
	}
	// Post-mortem: every window/door opening still without any space boundary is a
	// lost subsurface — log them with the data needed to debug (IFCC_DEBUG_OPENING_ID).
	for(const Opening& op : openings) {
		if(op.hasSpaceBoundary())
			continue;
		bool isWindowOrDoor = false;
		for(int eid : op.openingElementIds()) {
			std::shared_ptr<BuildingElement> be = buildingElements.fromID(eid);
			if(be && (be->type() == BET_Window || be->type() == BET_Door)) {
				isWindowOrDoor = true;
				break;
			}
		}
		if(isWindowOrDoor) {
			Logger::instance() << "Building::updateStoreys: UNMATCHED opening id=" << op.m_id
							   << " guid=" << op.guid() << " name='" << op.m_name << "'";
		}
	}

	return true;
}


TiXmlElement * Building::writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Building");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_ifcId));
//	e->SetAttribute("visible", IBK::val2string<bool>(true));

	if(!m_storeys.empty()) {
		TiXmlElement * child = new TiXmlElement("BuildingLevels");
		e->LinkEndChild(child);

		for( const auto& storey : m_storeys) {
			storey->writeXML(child, convertOptions);
		}
	}
	return e;
}

VICUS::Building Building::getVicusObject(const ConvertOptions& options) const {
	VICUS::Building res;
	res.m_id = m_id;
	if(!m_name.empty())
		res.m_displayName = QString::fromStdString(m_name + "_" + std::to_string(m_ifcId));
	res.m_ifcGUID = m_guid;
	for(const auto& storey : m_storeys) {
		res.m_buildingLevels.emplace_back(storey->getVicusObject(options));
	}

	return res;
}

} // namespace IFCC
