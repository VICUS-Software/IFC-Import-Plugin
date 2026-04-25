#ifndef IFCC_ClipperToolsH
#define IFCC_ClipperToolsH

#include <clipper.hpp>

#include <carve/matrix.hpp>

#include "IFCC_Types.h"

namespace IFCC {

class PlaneNormal;

/*! Contains results of function intersectPolygons2.
*/
struct IntersectionResult {

	/*! Result is only valid if at least one intersection exist and contains a valid polygon.*/
	bool isValid() const {
		if(m_intersections.empty())
			return false;
		for(const auto& poly : m_intersections)
			if(poly.size() > 2)
				return true;

		return false;
	}

	/*! Vector of intersection polygons.*/
	std::vector<polygon3D_t>				m_intersections;

	/*! Vector of hole polygones for each existing intersection-polygon.
		First dimension must be the same as m_intersections.
	*/
	std::vector<std::vector<polygon3D_t>>	m_holesIntersections;

	/*! Total number of childs of all holes in intersections.*/
	int										m_holesIntersectionChildCount = 0;

	/*! Vector of polygons from operation 'BasePolygon - ClipPolygon'*/
	std::vector<polygon3D_t>				m_diffBaseMinusClip;

	/*! Vector of hole polygones for each existing diffBaseMinusClip-polygon.
		First dimension must be the same as m_diffBaseMinusClip.
	*/
	std::vector<std::vector<polygon3D_t>>	m_holesBaseMinusClip;

	/*! Total number of childs of all holes in BaseMinusClip.*/
	int										m_holesBaseMinusClipChildCount = 0;

	/*! Vector of polygons from operation 'ClipPolygon - BasePolygon'*/
	std::vector<polygon3D_t>				m_diffClipMinusBase;

	/*! Vector of hole polygones for each existing diffClipMinusBase-polygon.
		First dimension must be the same as m_diffClipMinusBase.
	*/
	std::vector<std::vector<polygon3D_t>>	m_holesClipMinusBase;

	/*! Total number of childs of all holes in ClipMinusBase.*/
	int										m_holesClipMinusBaseChildCount = 0;
};

/*! Polygon to_merge will be merged into polygon base.
	The resulting polygon will be returned.
	The function uses internally the clipper function. This includes a conversion of the given 3D polygons into 2D polygons of clipper path type.
	\param base Base polygon for merging
	\param to_merge Polygon for merging into base polygon
	\param plane Plane in 3D in normal form for internal 3D to 2D and back conversion.
*/
polygon3D_t mergePolygons(const polygon3D_t& base, const polygon3D_t& to_merge, const PlaneNormal& plane);

/*! One connected outer ring from a planar union, together with its holes.
	Also carries the plane basis used during the 2D↔3D projection so downstream
	consumers can reconstruct a Polygon3D without re-inferring the plane from
	vertices (which is fragile for thin/self-touching inputs). */
struct CoplanarUnionRing {
	polygon3D_t					m_outer;	///< Outer (positively-oriented) boundary in 3D
	std::vector<polygon3D_t>	m_holes;	///< Inner (negatively-oriented) boundaries in 3D
	polygon2D_t					m_outer2D;	///< Outer ring in the plane's 2D basis (same vertex order as m_outer)
	std::vector<polygon2D_t>	m_holes2D;	///< Hole rings in the plane's 2D basis
	IBKMK::Vector3D				m_planeOffset;	///< 3D offset of the 2D frame (= PlaneNormal::m_pos)
	IBKMK::Vector3D				m_planeNormal;	///< Unit normal (= PlaneNormal::m_lz)
	IBKMK::Vector3D				m_planeLocalX;	///< Unit local X (= PlaneNormal::m_lx)
};

/*! Compute the union of two 2D polygons. Returns the (single) merged polygon if
	the union is a connected region; returns the first polygon unchanged if union
	yields multiple disjoint pieces (caller decides whether to keep both). Empty
	on clipper failure.
	\param a First 2D polygon
	\param b Second 2D polygon
*/
polygon2D_t union2DPolygons(const polygon2D_t& a, const polygon2D_t& b);

/*! Compute the union of multiple coplanar 3D polygons on the given plane.
	Uses Clipper's PolyTree output so disjoint regions become separate rings and
	inner contours (holes) stay associated with their containing outer ring.
	\param polygons Input polygons, all expected to lie on the given plane.
	\param plane Plane used for 3D↔2D conversion.
	\return Outer rings with their holes. Empty if input is empty or clipper fails.
*/
std::vector<CoplanarUnionRing> unionCoplanarPolygons(const std::vector<polygon3D_t>& polygons, const PlaneNormal& plane);

/*! Returns the intersection area of intersectPoly in base polygon.
	The function uses internally the clipper function. This includes a conversion of the given 3D polygons into 2D polygons of clipper path type.
	\param base Base polygon for calculation intersection.
	\param intersectPoly Polygon which intersects base polygon
	\param plane Plane in 3D in normal form for internal 3D to 2D and back conversion.
	\return Intersection polygon. It is not valid (empty) in case of no intersection exist.
*/
polygon3D_t intersectPolygons(const polygon3D_t& base, const polygon3D_t& intersectPoly, const PlaneNormal& plane);

/*! Creat a simple bounding rectangle for the given path.*/
ClipperLib::Path boundingPath(const ClipperLib::Path& base);

/*! Create an intersection with its own bounding rect.*/
std::vector<polygon3D_t> intersectBoundingRect(const polygon3D_t& intersectPoly, const PlaneNormal& plane);

/*! Calculates all polygons which can be calculated by intersection of intersectPoly into base.
	The result will contain the intersection polygon, the rest base polygon and the rest inetsect polygon including existing holes.
	\param base Base polygon for calculation intersection.
	\param intersectPoly Polygon which intersects base polygon
	\param plane Plane in 3D in normal form for internal 3D to 2D and back conversion.
	\return All resulting polygons with holes (if exists). \sa IntersectionResult
*/
IntersectionResult intersectPolygons2(const polygon3D_t& base, const polygon3D_t& intersectPoly, const PlaneNormal& plane);

/*! Try to simplify the given polygon. It return a vector of resulting polygons.
 *  The resulting vector is empty in case of errors like non valid base polygon.
*/
std::vector<polygon3D_t> simplifyPolygon(const polygon3D_t& base);

/*! Clean the given polygon by checking close points and colinear lines.
 *  The given polygon will be changed if some problems found.
 *  \param base Base polygon to check
 *  \param distance Minimum distance for close point checking in m
*/
void cleanPolygon(polygon3D_t &base, double distance = 1e-5);

} // namespace IFCC

#endif // IFCC_ClipperToolsH
