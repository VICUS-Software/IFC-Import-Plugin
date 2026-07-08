#include "IFCC_RoomHealer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include <IBKMK_Vector3D.h>
#include <IBKMK_Polygon3D.h>
#include <IBKMK_2DCalculations.h>

#include <VICUS_Project.h>
#include <VICUS_Room.h>
#include <VICUS_Surface.h>
#include <VICUS_IFCDrawing.h>

#include "IFCC_Clippertools.h"
#include "IFCC_GeometricHelperClasses.h"
#include "IFCC_Logger.h"
#include "IFCC_Types.h"

namespace IFCC {

namespace {

/*! Status rank for the safety net: 0 = valid, 1 = warning, 2 = error. */
int statusRank(const VICUS::Room& r) {
	switch (r.roomStatus()) {
		case VICUS::Room::RS_Valid:	  return 0;
		case VICUS::Room::RS_Warning: return 1;
		default:					  return 2;
	}
}

IBKMK::Vector3D newellNormal(const std::vector<IBKMK::Vector3D>& poly) {
	IBKMK::Vector3D n(0,0,0);
	const size_t cnt = poly.size();
	for(size_t i=0; i<cnt; ++i) {
		const IBKMK::Vector3D& a = poly[i];
		const IBKMK::Vector3D& b = poly[(i+1)%cnt];
		n.m_x += (a.m_y - b.m_y) * (a.m_z + b.m_z);
		n.m_y += (a.m_z - b.m_z) * (a.m_x + b.m_x);
		n.m_z += (a.m_x - b.m_x) * (a.m_y + b.m_y);
	}
	return n * 0.5;
}

double polygonArea3D(const std::vector<IBKMK::Vector3D>& poly) {
	return newellNormal(poly).magnitude();
}

/*! Signed volume contribution of one polygon fan w.r.t. reference point c.
	Positive when the polygon winding is outward as seen from c. */
double signedVolumeContribution(const std::vector<IBKMK::Vector3D>& poly, const IBKMK::Vector3D& c) {
	double v = 0.0;
	if(poly.size() < 3)
		return 0.0;
	const IBKMK::Vector3D p0 = poly[0] - c;
	for(size_t i=1; i+1<poly.size(); ++i) {
		const IBKMK::Vector3D p1 = poly[i] - c;
		const IBKMK::Vector3D p2 = poly[i+1] - c;
		v += p0.scalarProduct(p1.crossProduct(p2));
	}
	return v / 6.0;
}

IBKMK::Vector3D bboxCenter(const std::vector<VICUS::Surface>& surfs) {
	IBKMK::Vector3D mn(1e20,1e20,1e20), mx(-1e20,-1e20,-1e20);
	for(const VICUS::Surface& s : surfs) {
		for(const IBKMK::Vector3D& v : s.polygon3D().vertexes()) {
			mn.m_x = std::min(mn.m_x, v.m_x); mn.m_y = std::min(mn.m_y, v.m_y); mn.m_z = std::min(mn.m_z, v.m_z);
			mx.m_x = std::max(mx.m_x, v.m_x); mx.m_y = std::max(mx.m_y, v.m_y); mx.m_z = std::max(mx.m_z, v.m_z);
		}
	}
	return (mn + mx) * 0.5;
}

// ---------------------------------------------------------------------------
// Pass 0 — drop broken subsurfaces (invalid polygon, outside the parent surface,
// overlapping each other). Such holes make the parent surface untriangulatable —
// VICUS' shoelace volume then silently skips the whole face and the room volume
// collapses (WSHH: door holes wider than the wall from oversized opening hulls).
// ---------------------------------------------------------------------------

double area2D(const std::vector<IBKMK::Vector2D>& poly) {
	double a = 0.0;
	for(size_t i=0; i<poly.size(); ++i) {
		const IBKMK::Vector2D& p = poly[i];
		const IBKMK::Vector2D& q = poly[(i+1)%poly.size()];
		a += p.m_x*q.m_y - q.m_x*p.m_y;
	}
	return std::fabs(a)*0.5;
}

int dropBrokenSubsurfaces(std::vector<VICUS::Surface>& surfs, std::set<unsigned int>& droppedSubIds) {
	int dropped = 0;
	for(VICUS::Surface& s : surfs) {
		if(s.subSurfaces().empty())
			continue;
		const std::vector<IBKMK::Vector2D>& outer = s.geometry().polygon2D().vertexes();
		if(outer.size() < 3)
			continue;
		std::vector<VICUS::SubSurface> kept;
		std::vector<const std::vector<IBKMK::Vector2D>*> keptPolys;
		bool changed = false;
		for(const VICUS::SubSurface& sub : s.subSurfaces()) {
			const std::vector<IBKMK::Vector2D>& sp = sub.m_polygon2D.vertexes();
			bool ok = sub.m_polygon2D.isValid() && sp.size() >= 3;
			// must lie inside the parent polygon (small tolerance for edge-touching)
			if(ok) {
				for(const IBKMK::Vector2D& v : sp) {
					if(IBKMK::pointInPolygonFuzzy(outer, v, 0.02) < 0) {
						ok = false;
						break;
					}
				}
			}
			// must not overlap an already accepted subsurface
			if(ok) {
				double aSub = area2D(sp);
				for(const auto* kp : keptPolys) {
					polygon2D_t uni = union2DPolygons(sp, *kp);
					if(uni.empty())
						continue;
					double aUni = area2D(uni);
					if(aUni < aSub + area2D(*kp) - 0.01) {
						ok = false; // overlaps an earlier hole
						break;
					}
				}
			}
			if(ok) {
				kept.push_back(sub);
				keptPolys.push_back(&sub.m_polygon2D.vertexes());
			}
			else {
				droppedSubIds.insert(sub.m_id);
				++dropped;
				changed = true;
				Logger::instance() << "room-heal: drop broken subsurface '" << sub.m_displayName.toStdString()
								   << "' of surface '" << s.m_displayName.toStdString() << "'";
			}
		}
		if(changed)
			s.setSubSurfaces(kept);
	}
	return dropped;
}

// ---------------------------------------------------------------------------
// Pass 1 — coplanar duplicate/overlap clipping
// ---------------------------------------------------------------------------

struct Pass1Result {
	int m_dropped = 0;
	int m_clipped = 0;
	std::set<unsigned int> m_droppedIds;
};

Pass1Result clipCoplanarOverlaps(std::vector<VICUS::Surface>& surfs) {
	Pass1Result res;
	const size_t n = surfs.size();
	if(n < 2 || n > 400)
		return res;

	struct Info {
		IBKMK::Vector3D n;		// unit normal
		double			d = 0;	// plane offset (n * p), sign-normalized below
		double			area = 0;
		bool			protectedSurf = false;
		bool			dropped = false;
	};
	std::vector<Info> info(n);
	for(size_t i=0; i<n; ++i) {
		const std::vector<IBKMK::Vector3D>& poly = surfs[i].polygon3D().vertexes();
		IBKMK::Vector3D nn = newellNormal(poly);
		double len = nn.magnitude();
		if(len < 1e-9 || poly.size() < 3) {
			info[i].area = 0;
			continue;
		}
		info[i].n = nn * (1.0/len);
		info[i].area = len;
		IBKMK::Vector3D centroid(0,0,0);
		for(const IBKMK::Vector3D& v : poly)
			centroid += v;
		centroid *= 1.0/double(poly.size());
		info[i].d = info[i].n.scalarProduct(centroid);
		info[i].protectedSurf = !surfs[i].subSurfaces().empty();
	}

	// Order in which surfaces claim their area: protected (with subsurfaces) first,
	// then by area descending — later (smaller) surfaces get clipped against earlier.
	std::vector<size_t> order(n);
	for(size_t i=0; i<n; ++i) order[i] = i;
	std::sort(order.begin(), order.end(), [&info](size_t a, size_t b) {
		if(info[a].protectedSurf != info[b].protectedSurf)
			return info[a].protectedSurf;
		return info[a].area > info[b].area;
	});

	const double kPlaneDist = 0.02;		// [m] max plane offset for "coplanar"
	const double kMinParallel = 0.999;	// |cos| of normals

	for(size_t oi=1; oi<n; ++oi) {
		size_t i = order[oi];
		if(info[i].area < 1e-6 || info[i].protectedSurf || info[i].dropped)
			continue;
		// clip surface i against every earlier surface in the same plane
		for(size_t oj=0; oj<oi; ++oj) {
			size_t j = order[oj];
			if(info[j].dropped || info[j].area < 1e-6)
				continue;
			double dot = info[i].n.scalarProduct(info[j].n);
			if(std::fabs(dot) < kMinParallel)
				continue;
			double dj = dot > 0 ? info[j].d : -info[j].d;
			if(std::fabs(info[i].d - dj) > kPlaneDist)
				continue;

			const polygon3D_t& polyI = surfs[i].polygon3D().vertexes();
			const polygon3D_t& polyJ = surfs[j].polygon3D().vertexes();
			PlaneNormal plane(polyJ);
			IntersectionResult ir = intersectPolygons2(polyJ, polyI, plane);
			double interArea = 0.0;
			for(const polygon3D_t& p : ir.m_intersections)
				interArea += polygonArea3D(p);
			if(interArea < 0.01)
				continue;
			if(interArea >= 0.99 * info[i].area) {
				// fully covered by an earlier (bigger/protected) surface -> duplicate
				info[i].dropped = true;
				res.m_droppedIds.insert(surfs[i].m_id);
				++res.m_dropped;
				break;
			}
			// partial overlap: keep only the non-covered remainder if it is a single
			// clean polygon — everything else stays untouched (no data loss).
			if(ir.m_diffClipMinusBase.size() == 1 && ir.m_holesClipMinusBase[0].empty()) {
				const polygon3D_t& rest = ir.m_diffClipMinusBase.front();
				double restArea = polygonArea3D(rest);
				if(rest.size() >= 3 && restArea > 0.01 && restArea < info[i].area) {
					IBKMK::Polygon3D p3(rest);
					if(p3.isValid()) {
						// keep original winding orientation
						IBKMK::Vector3D nRest = newellNormal(rest);
						if(nRest.scalarProduct(info[i].n) < 0) {
							polygon3D_t rev(rest.rbegin(), rest.rend());
							p3 = IBKMK::Polygon3D(rev);
						}
						if(p3.isValid()) {
							surfs[i].setPolygon3D(p3);
							info[i].area = restArea;
							++res.m_clipped;
						}
					}
				}
			}
		}
	}
	if(res.m_dropped > 0) {
		std::vector<VICUS::Surface> kept;
		kept.reserve(n);
		for(size_t i=0; i<n; ++i)
			if(!info[i].dropped)
				kept.push_back(surfs[i]);
		surfs.swap(kept);
	}
	return res;
}

// ---------------------------------------------------------------------------
// Pass 1b — coalesce coplanar 'Missing' fill fragments. The shell-fill passes
// produce dozens of small Missing pieces per plane (WSHH cellar: 31 in one room
// around an excluded column). Merging them into their union polygon removes the
// fragmentation without changing the covered area.
// ---------------------------------------------------------------------------

struct CoalesceResult {
	int m_merged = 0;					///< surfaces merged away
	std::set<unsigned int> m_droppedIds;
};

CoalesceResult coalesceMissingFills(std::vector<VICUS::Surface>& surfs) {
	CoalesceResult res;
	// collect indices of mergeable Missing pieces
	std::vector<size_t> cand;
	for(size_t i=0; i<surfs.size(); ++i) {
		if(surfs[i].m_displayName != "Missing" || !surfs[i].subSurfaces().empty())
			continue;
		if(surfs[i].polygon3D().vertexes().size() >= 3)
			cand.push_back(i);
	}
	if(cand.size() < 2)
		return res;

	// group by plane
	struct Group {
		IBKMK::Vector3D n;
		double d;
		std::vector<size_t> members;
	};
	std::vector<Group> groups;
	for(size_t i : cand) {
		const std::vector<IBKMK::Vector3D>& poly = surfs[i].polygon3D().vertexes();
		IBKMK::Vector3D nn = newellNormal(poly);
		double len = nn.magnitude();
		if(len < 1e-9)
			continue;
		nn *= 1.0/len;
		IBKMK::Vector3D c(0,0,0);
		for(const IBKMK::Vector3D& v : poly) c += v;
		c *= 1.0/double(poly.size());
		double d = nn.scalarProduct(c);
		bool found = false;
		for(Group& g : groups) {
			double dot = g.n.scalarProduct(nn);
			double dg = dot > 0 ? d : -d;
			if(std::fabs(dot) >= 0.999 && std::fabs(g.d - dg) <= 0.02) {
				g.members.push_back(i);
				found = true;
				break;
			}
		}
		if(!found)
			groups.push_back(Group{nn, d, {i}});
	}

	std::set<size_t> toDrop;
	for(const Group& g : groups) {
		if(g.members.size() < 2)
			continue;
		std::vector<polygon3D_t> polys;
		for(size_t i : g.members)
			polys.push_back(surfs[i].polygon3D().vertexes());
		PlaneNormal plane(polys.front());
		if(!plane.m_valid)
			continue;
		std::vector<CoplanarUnionRing> rings = unionCoplanarPolygons(polys, plane);
		// Only apply when the union actually reduces the count and yields clean
		// hole-free rings — otherwise keep the original pieces (no data loss).
		if(rings.empty() || rings.size() >= g.members.size())
			continue;
		bool clean = true;
		double ringArea = 0.0, inArea = 0.0;
		for(const CoplanarUnionRing& r : rings) {
			if(!r.m_holes.empty() || r.m_outer.size() < 3) {
				clean = false;
				break;
			}
			ringArea += polygonArea3D(r.m_outer);
		}
		for(const polygon3D_t& p : polys)
			inArea += polygonArea3D(p);
		// area sanity: union must not exceed the input area noticeably (disjoint
		// pieces keep their area; overlapping input shrinks slightly)
		if(!clean || ringArea > inArea * 1.05 + 0.01)
			continue;
		// first N members become the merged rings, the rest is dropped
		size_t ri = 0;
		for(; ri < rings.size(); ++ri) {
			polygon3D_t outer = rings[ri].m_outer;
			// realign winding to the group plane normal
			IBKMK::Vector3D rn = newellNormal(outer);
			if(rn.scalarProduct(g.n) < 0)
				outer.assign(rings[ri].m_outer.rbegin(), rings[ri].m_outer.rend());
			IBKMK::Polygon3D p3(outer);
			if(!p3.isValid())
				break;
			surfs[g.members[ri]].setPolygon3D(p3);
		}
		if(ri < rings.size())
			continue; // some ring failed to convert -> leave the remaining originals
		for(size_t mi = rings.size(); mi < g.members.size(); ++mi) {
			toDrop.insert(g.members[mi]);
			res.m_droppedIds.insert(surfs[g.members[mi]].m_id);
		}
		res.m_merged += int(g.members.size() - rings.size());
	}
	if(!toDrop.empty()) {
		std::vector<VICUS::Surface> kept;
		kept.reserve(surfs.size());
		for(size_t i=0; i<surfs.size(); ++i)
			if(!toDrop.count(i))
				kept.push_back(surfs[i]);
		surfs.swap(kept);
	}
	return res;
}

// ---------------------------------------------------------------------------
// Pass 2 — winding repair via edge-adjacency propagation
// ---------------------------------------------------------------------------

int repairWinding(std::vector<VICUS::Surface>& surfs) {
	const size_t n = surfs.size();
	if(n < 2)
		return 0;

	// Quantized vertex key (1 mm grid) — faces generated from the same shell share
	// exact vertices after the anchoring passes, so exact-key edge matching finds
	// most adjacencies. Unmatched faces simply stay in their own component.
	auto vkey = [](const IBKMK::Vector3D& v) -> std::array<long long,3> {
		return { std::llround(v.m_x*1000.0), std::llround(v.m_y*1000.0), std::llround(v.m_z*1000.0) };
	};
	struct EdgeUse {
		size_t	face;
		bool	forward;	// edge traversed from lower key to higher key
	};
	std::map<std::pair<std::array<long long,3>, std::array<long long,3>>, std::vector<EdgeUse>> edges;
	for(size_t i=0; i<n; ++i) {
		const std::vector<IBKMK::Vector3D>& poly = surfs[i].polygon3D().vertexes();
		const size_t cnt = poly.size();
		for(size_t k=0; k<cnt; ++k) {
			auto a = vkey(poly[k]);
			auto b = vkey(poly[(k+1)%cnt]);
			if(a == b)
				continue;
			bool fwd = a < b;
			if(!fwd)
				std::swap(a, b);
			edges[{a, b}].push_back(EdgeUse{i, fwd});
		}
	}

	// Constraint graph: edge shared by exactly two faces. Opposite traversal =>
	// same orientation class (parity 0), same traversal => one face needs a flip
	// (parity 1). BFS 2-coloring; conflicting constraints are skipped.
	std::vector<std::vector<std::pair<size_t,int>>> adj(n);
	for(const auto& e : edges) {
		if(e.second.size() != 2)
			continue;
		const EdgeUse& u1 = e.second[0];
		const EdgeUse& u2 = e.second[1];
		if(u1.face == u2.face)
			continue;
		int parity = (u1.forward == u2.forward) ? 1 : 0;
		adj[u1.face].push_back({u2.face, parity});
		adj[u2.face].push_back({u1.face, parity});
	}

	std::vector<int> comp(n, -1), flip(n, 0);
	std::vector<std::vector<size_t>> components;
	for(size_t i=0; i<n; ++i) {
		if(comp[i] != -1)
			continue;
		int ci = int(components.size());
		components.push_back({});
		std::vector<size_t> stack{i};
		comp[i] = ci;
		flip[i] = 0;
		while(!stack.empty()) {
			size_t f = stack.back();
			stack.pop_back();
			components[ci].push_back(f);
			for(const auto& nb : adj[f]) {
				int want = flip[f] ^ nb.second;
				if(comp[nb.first] == -1) {
					comp[nb.first] = ci;
					flip[nb.first] = want;
					stack.push_back(nb.first);
				}
				// conflicting constraint (non-orientable patch): ignored
			}
		}
	}

	// Decide the global flip of each component from its signed volume contribution
	// w.r.t. the room center: outward winding gives a positive contribution.
	const IBKMK::Vector3D center = bboxCenter(surfs);
	int flipped = 0;
	for(const std::vector<size_t>& faces : components) {
		double vol = 0.0;
		for(size_t f : faces) {
			double c = signedVolumeContribution(surfs[f].polygon3D().vertexes(), center);
			vol += flip[f] ? -c : c;
		}
		const bool invertComponent = vol < 0.0;
		for(size_t f : faces) {
			bool doFlip = bool(flip[f]) != invertComponent;
			if(doFlip) {
				surfs[f].flip();
				++flipped;
			}
		}
	}
	return flipped;
}

// ---------------------------------------------------------------------------
// Pass 3 — close remaining shell holes with the room's closing polygons
// ---------------------------------------------------------------------------

int closeHoles(VICUS::Room& room, unsigned int& nextId) {
	const std::vector<IBKMK::Polygon3D>& closers = room.closingPolygons();
	if(closers.empty() || closers.size() > 30)
		return 0;
	std::vector<VICUS::Surface> surfs = room.surfaces();
	const IBKMK::Vector3D center = bboxCenter(surfs);
	int added = 0;
	for(const IBKMK::Polygon3D& poly : closers) {
		if(!poly.isValid())
			continue;
		double area = polygonArea3D(poly.vertexes());
		if(area < 0.01 || area > 500.0)
			continue;
		IBKMK::Polygon3D oriented = poly;
		if(signedVolumeContribution(poly.vertexes(), center) < 0.0) {
			std::vector<IBKMK::Vector3D> rev(poly.vertexes().rbegin(), poly.vertexes().rend());
			oriented = IBKMK::Polygon3D(rev);
			if(!oriented.isValid())
				oriented = poly;
		}
		VICUS::Surface s;
		s.m_id = nextId++;
		s.m_displayName = "Missing";
		s.setPolygon3D(oriented);
		if(!s.geometry().isValid())
			continue;
		surfs.push_back(s);
		++added;
	}
	if(added > 0)
		room.setSurfaces(surfs);

	// Pass 3b: close QUADS spanned by pairs of remaining uncovered edges.
	// closingPolygons only chains planar loops — the typical leftovers are
	// (a) excluded-column notches: three Missing side faces exist, the fourth
	//     0.5x3.4m slot face is missing (two parallel full-height open edges),
	// (b) trapezoid floor/ceiling strips over wedges between wall layers
	//     (two coplanar horizontal open edges of different length).
	{
		const std::vector<VICUS::Room::UncoveredSegment>& segs = room.uncoveredSegments();
		const size_t n2 = segs.size();
		if(n2 >= 2 && n2 <= 60) {
			std::vector<VICUS::Surface> cur = room.surfaces();
			std::vector<bool> used(n2, false);
			int quads = 0;
			const IBKMK::Vector3D center = bboxCenter(cur);
			for(size_t i=0; i<n2 && quads < 20; ++i) {
				if(used[i]) continue;
				const IBKMK::Vector3D ai = segs[i].m_start, bi = segs[i].m_end;
				double li = (bi-ai).magnitude();
				if(li < 0.05) continue;
				for(size_t j=i+1; j<n2; ++j) {
					if(used[j]) continue;
					const IBKMK::Vector3D aj = segs[j].m_start, bj = segs[j].m_end;
					double lj = (bj-aj).magnitude();
					if(lj < 0.05) continue;
					// edges must be roughly parallel and near each other
					IBKMK::Vector3D di = (bi-ai)*(1.0/li), dj = (bj-aj)*(1.0/lj);
					double dot = di.scalarProduct(dj);
					if(std::fabs(dot) < 0.95)
						continue;
					double gap = ((ai+bi)*0.5 - (aj+bj)*0.5).magnitude();
					if(gap < 0.02 || gap > 2.0)
						continue;
					// build quad (reverse j when running the same direction)
					std::vector<IBKMK::Vector3D> quad;
					if(dot > 0)
						quad = {ai, bi, bj, aj};
					else
						quad = {ai, bi, aj, bj};
					// planarity: all points within 5 cm of the quad plane
					IBKMK::Vector3D qn = newellNormal(quad);
					double qlen = qn.magnitude();
					if(qlen < 1e-6)
						continue;
					qn *= 1.0/qlen;
					double d0 = qn.scalarProduct(quad[0]);
					bool planar = true;
					for(const IBKMK::Vector3D& v : quad) {
						if(std::fabs(qn.scalarProduct(v) - d0) > 0.05) {
							planar = false;
							break;
						}
					}
					if(!planar)
						continue;
					double area = polygonArea3D(quad);
					if(area < 0.03 || area > 60.0)
						continue;
					if(signedVolumeContribution(quad, center) < 0.0)
						std::reverse(quad.begin(), quad.end());
					IBKMK::Polygon3D p3(quad);
					if(!p3.isValid())
						continue;
					VICUS::Surface s;
					s.m_id = nextId++;
					s.m_displayName = "Missing";
					s.setPolygon3D(p3);
					if(!s.geometry().isValid())
						continue;
					cur.push_back(s);
					used[i] = used[j] = true;
					++quads;
					break;
				}
			}
			if(quads > 0) {
				room.setSurfaces(cur);
				added += quads;
			}
		}
	}
	return added;
}

} // anonymous namespace


RoomHealStats healRooms(VICUS::Project& prj) {
	RoomHealStats stats;
	if(std::getenv("IFCC_NO_ROOMHEAL") != nullptr) {
		Logger::instance() << "room-heal: disabled via IFCC_NO_ROOMHEAL";
		return stats;
	}

	// nextUnusedID() works off the m_objectPtr cache (updatePointers), which does not
	// know the IFC drawing ids assigned right before this pass — account them manually,
	// otherwise the new Missing surfaces collide with the IFCDrawing id on re-read.
	unsigned int nextId = prj.nextUnusedID();
	auto bumpPast = [&nextId](unsigned int id) {
		if(id != INVALID_ID && id >= nextId)
			nextId = id + 1;
	};
	bumpPast(prj.m_ifc.m_id);
	for(const VICUS::IFCDrawing& d : prj.m_ifc.m_drawings) {
		bumpPast(d.m_id);
		bumpPast(d.m_rootObjectNode.m_id);
		for(const VICUS::IFCObjectNode& n : d.m_objectNodes)
			bumpPast(n.m_id);
	}
	std::set<unsigned int> allDroppedIds;
	std::set<unsigned int> allDroppedSubIds;

	for(VICUS::Building& b : prj.m_buildings) {
		for(VICUS::BuildingLevel& bl : b.m_buildingLevels) {
			for(VICUS::Room& room : bl.m_rooms) {
				if(room.surfaces().empty())
					continue;
				++stats.m_roomsProcessed;

				const int rankBefore = statusRank(room);
				if(rankBefore == 2) ++stats.m_errBefore;
				else if(rankBefore == 1) ++stats.m_warnBefore;
				if(rankBefore == 0) {
					// nothing to heal
					continue;
				}
				const std::vector<VICUS::Surface> backup = room.surfaces();

				std::vector<VICUS::Surface> surfs = room.surfaces();
				std::set<unsigned int> droppedSubIdsHere;
				int subsDroppedHere = dropBrokenSubsurfaces(surfs, droppedSubIdsHere);
				Pass1Result p1 = clipCoplanarOverlaps(surfs);
				CoalesceResult coal = coalesceMissingFills(surfs);
				int flippedHere = repairWinding(surfs);
				room.setSurfaces(surfs);
				int holesHere = closeHoles(room, nextId);

				const int rankAfter = statusRank(room);
				if(rankAfter > rankBefore) {
					// safety net: healing made it worse -> revert everything
					room.setSurfaces(backup);
					++stats.m_roomsReverted;
					Logger::instance() << "room-heal: REVERT room '" << room.m_displayName.toStdString()
									   << "' rank " << rankBefore << " -> " << rankAfter;
					if(rankBefore == 2) ++stats.m_errAfter;
					else ++stats.m_warnAfter;
					continue;
				}
				stats.m_duplicatesDropped += p1.m_dropped;
				stats.m_surfacesClipped += p1.m_clipped;
				stats.m_surfacesFlipped += flippedHere;
				stats.m_holesClosed += holesHere;
				stats.m_subsurfacesDropped += subsDroppedHere;
				stats.m_fillsCoalesced += coal.m_merged;
				allDroppedIds.insert(p1.m_droppedIds.begin(), p1.m_droppedIds.end());
				allDroppedIds.insert(coal.m_droppedIds.begin(), coal.m_droppedIds.end());
				allDroppedSubIds.insert(droppedSubIdsHere.begin(), droppedSubIdsHere.end());
				if(rankAfter == 2) ++stats.m_errAfter;
				else if(rankAfter == 1) ++stats.m_warnAfter;
				if(rankAfter != rankBefore || p1.m_dropped || flippedHere || holesHere) {
					Logger::instance() << "room-heal: room '" << room.m_displayName.toStdString()
									   << "' rank " << rankBefore << " -> " << rankAfter
									   << " dropped=" << p1.m_dropped << " clipped=" << p1.m_clipped
									   << " flipped=" << flippedHere << " holesClosed=" << holesHere;
				}
			}
		}
	}

	// Remove component instances that reference dropped surfaces.
	if(!allDroppedIds.empty()) {
		std::vector<VICUS::ComponentInstance> kept;
		kept.reserve(prj.m_componentInstances.size());
		for(const VICUS::ComponentInstance& ci : prj.m_componentInstances) {
			if(allDroppedIds.count(ci.m_sideA.m_idSurface) || allDroppedIds.count(ci.m_sideB.m_idSurface))
				continue;
			kept.push_back(ci);
		}
		prj.m_componentInstances.swap(kept);
	}
	// Remove subsurface component instances that reference dropped subsurfaces.
	if(!allDroppedSubIds.empty()) {
		std::vector<VICUS::SubSurfaceComponentInstance> kept;
		kept.reserve(prj.m_subSurfaceComponentInstances.size());
		for(const VICUS::SubSurfaceComponentInstance& ci : prj.m_subSurfaceComponentInstances) {
			if(allDroppedSubIds.count(ci.m_sideA.m_idSurface) || allDroppedSubIds.count(ci.m_sideB.m_idSurface))
				continue;
			kept.push_back(ci);
		}
		prj.m_subSurfaceComponentInstances.swap(kept);
	}

	Logger::instance() << "room-heal: SUMMARY rooms=" << stats.m_roomsProcessed
					   << " err " << stats.m_errBefore << "->" << stats.m_errAfter
					   << " warn " << stats.m_warnBefore << "->" << stats.m_warnAfter
					   << " dropped=" << stats.m_duplicatesDropped
					   << " clipped=" << stats.m_surfacesClipped
					   << " flipped=" << stats.m_surfacesFlipped
					   << " holesClosed=" << stats.m_holesClosed
					   << " subsDropped=" << stats.m_subsurfacesDropped
					   << " fillsCoalesced=" << stats.m_fillsCoalesced
					   << " reverted=" << stats.m_roomsReverted;
	return stats;
}

} // namespace IFCC
