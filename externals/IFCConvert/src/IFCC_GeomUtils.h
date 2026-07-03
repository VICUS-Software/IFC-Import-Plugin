/* -*-c++-*- IfcQuery www.ifcquery.com
*
MIT License

Copyright (c) 2017 Fabian Gerold

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <array>
#include <vector>

#include <ifcpp/model/BasicTypes.h>
#include <ifcpp/model/BuildingException.h>

// Stock GLM guards its gtx extensions behind GLM_ENABLE_EXPERIMENTAL and #errors
// out otherwise. The previously bundled ifcpp glm had this patched into its headers;
// our glm lib does not, so we must enable it before including any gtx/gtc header.
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm.hpp>
#include <gtx/closest_point.hpp>
#include <gtx/intersect.hpp>
#include <gtx/matrix_decompose.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtx/normal.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/string_cast.hpp>
#include <gtx/vector_angle.hpp>

#include <ifcpp/geometry/IncludeCarveHeaders.h>

namespace IFCC {


#define EPS_M16 1e-16
#define EPS_M14 1e-14
#define EPS_M12 1e-12
#define EPS_M9 1e-9
#define EPS_M8 1e-8
#define EPS_M7 1e-7
#define EPS_M6 1e-6
#define EPS_M5 1e-5
#define EPS_M4 1e-4
#define EPS_DEFAULT 1.5*EPS_M9
#define EPS_ALIGNED_EDGES 1e-12
#define EPS_ANGLE_COPLANAR_FACES 1e-13

typedef std::array<double, 2> array2d;
class GeometrySettings;

namespace GeomUtils
{
	inline double length2( const carve::geom::vector<3>& p0, const carve::geom::vector<3>& p1 )
	{
		double dx = p1.x - p0.x;
		double dy = p1.y - p0.y;
		double dz = p1.z - p0.z;
		return dx * dx + dy * dy + dz * dz;
	}

	inline void safeNormalize(carve::geom::vector<3>& vec)
	{
		double len = vec.length();
		if( len > EPS_DEFAULT )
		{
			vec.normalize();
		}
	}

	inline void safeNormalize(glm::dvec3& vec)
	{
		double len = glm::length(vec);
		if( len > EPS_DEFAULT )
		{
			vec = glm::normalize(vec);
		}
	}

	enum ProjectionPlane {
		UNDEFINED,
		XY_PLANE,
		YZ_PLANE,
		XZ_PLANE
	};

	class Ray
	{
	public:
		void setRay( Ray* other );

		void setRay( const Ray& other );

		glm::dvec3 origin;
		glm::dvec3 direction;
	};

	struct Plane
	{
		Plane();

		Plane(const glm::dvec3& _position, const glm::dvec3& _normal);

		const glm::dvec3& getPosition() const { return m_position; }
		const glm::dvec3& getNormal() const { return m_normal; }
		const void setNormal( glm::dvec3& norm ) { m_normal = norm; }

		void setPlane(const glm::dvec3& _position, const glm::dvec3& _normal);

		// distance point to plane
		double distancePointPlane(const glm::dvec3& point) const;

		bool isEqualToPlane(const Plane& other, bool allowOppositeNormals, double epsDistance = EPS_DEFAULT, double epsAngle = EPS_ANGLE_COPLANAR_FACES) const;

	protected:
		glm::dvec3 m_position;
		glm::dvec3 m_normal;
	};

	/** polygon operations */
	vec3 computePolygonCentroid( const std::vector<vec3>& polygon );

	vec2 computePolygonCentroid( const std::vector<vec2>& polygon );

	vec3 computePolygonNormal( const std::vector<vec3>& polygon );

	vec3 computePolygonNormal( const std::vector<carve::mesh::Vertex<3>* >& polygon );

	vec3 computePolygon2DNormal( const std::vector<vec2>& polygon );

	glm::dvec3 computePolygon2DNormal(const std::vector<std::array<double, 2> >& polygon);

	bool isQuadConvex(const std::array<double, 2>& _a, const std::array<double, 2>& _b, const std::array<double, 2>& _c, const std::array<double, 2>& _d);

	bool checkOpenPolygonConvexity( const std::vector<vec2>& polygon );

	void appendPointsToCurve( const std::vector<vec2>& points_vec, std::vector<vec3>& target_vec );

	void appendPointsToCurve( const std::vector<vec3>& points_vec_src, std::vector<vec3>& target_vec );

	void addArcWithEndPoint( std::vector<vec2>& coords, double radius, double start_angle, double opening_angle, double x_center, double y_center, int num_segments );

	bool LineToLineIntersectionHelper( vec2& v1, vec2& v2, vec2& v3, vec2& v4, double & r, double & s );

	bool LineToLineIntersectionHelper( vec3& v1, vec3& v2, vec3& v3, vec3& v4, double & r, double & s );

	bool LineSegmentToLineIntersection( vec2& v1, vec2& v2, vec2& v3, vec2& v4, std::vector<vec2>& result );

	bool LineSegmentToLineSegmentIntersection( vec2& v1, vec2& v2, vec2& v3, vec2& v4, std::vector<vec2>& result );

	bool LineSegmentToLineSegmentIntersection( vec3& v1, vec3& v2, vec3& v3, vec3& v4, std::vector<vec3>& result );

	void closestPointOnLine( const vec3& point, const vec3& line_origin, const vec3& line_direction, vec3& closest );

	void closestPointOnLine( const vec2& point, const vec2& line_origin, const vec2& line_direction, vec2& closest );

	double distancePoint2Line( const vec3& point, const vec3& line_p0, const vec3& line_p1 );

	double distancePoint2LineUnitDirection( const vec3& point, const vec3& line_pt, const vec3& line_direction_normalized );

	void poly2VecToArray2(const std::vector<carve::geom::vector<2> >& polyIn, std::vector<std::array<double, 2> >& polyOut);

	void poly2VecToArray2(const std::vector<glm::dvec2>& polyIn, std::vector<std::array<double, 2> >& polyOut);

	void polygons2flatVec(const std::vector<vec2>& polyIn, std::vector<std::array<double, 2> >& polyOut);

	void polygons2flatVec(const std::vector<std::vector<vec2> >& polyIn, std::vector<std::array<double, 2> >& polyOut);

	void polygons2flatVec(const std::vector<std::vector<std::array<double, 2> > >& polyIn, std::vector<std::array<double, 2> >& polyOut);

	void polygons2flatVec(const std::vector<std::vector<vec2> >& polyIn, std::vector<vec2>& polyOut);

	void polygons2flatVec(const std::vector<std::vector<vec3> >& polyIn, std::vector<vec3>& polyOut);

	double signedArea(const std::vector<std::array<double, 2> >& points);

	double signedArea(const std::vector<carve::geom::vector<2> >& points);

	double computePolygonArea(const std::vector<vec3>& points);

	void unClosePolygon(std::vector<vec2>& polygon2d);

	void unClosePolygon(std::vector<vec3>& polygon3d);

	void closePolygon(std::vector<vec2>& polygon2d);

	void closePolygon(std::vector<vec3>& polygon3d);

	void simplifyPolygon(std::vector<std::array<double, 2> >& polygon, bool mergeAlignedEdges);

	void simplifyPolygon(std::vector<vec2>& polygon, bool mergeAlignedEdges);

	void simplifyPolygon(std::vector<vec3>& polygon, double epsMergePoints, double epsMergeAlignedEdges);

	void simplifyPolygon(std::vector<std::vector<std::array<double, 2> > >& vecOfPolygons, bool mergeAlignedEdges);

	void simplifyPolygon(std::vector<std::vector<vec2> >& vecOfPolygons, bool mergeAlignedEdges);

	template<unsigned ndim>
	double Point2LineSegmentDistance2( const carve::geom::linesegment<ndim> &l, const carve::geom::vector<ndim> &v, carve::geom::vector<ndim> &closest_point )
	{
		carve::geom::vector<ndim> linesegment_delta = l.v2 - l.v1;
		double t = dot( v - l.v1, linesegment_delta ) / dot( linesegment_delta, linesegment_delta );
		if( t <= 0.0 )
		{
			t = 0.0;
		}
		if( t >= 1.0 )
		{
			t = 1.0;
		}
		closest_point = linesegment_delta*t + l.v1;
		return ( v - closest_point ).length2();
	}

	bool isMatrixIdentity( const carve::math::Matrix& mat, double eps = 0.00001 );

	bool isMatrixEqual(const carve::math::Matrix& mat1, const carve::math::Matrix& mat2, double maxDelta = 0.00001);

	void makeLookAt(const vec3& eye,const vec3& center,const vec3& up, carve::math::Matrix& resulting_matrix );

	bool bisectingPlane( const vec3& v1, const vec3& v2, const vec3& v3, vec3& normal, double CARVE_EPSILON );

	void convertPlane2Matrix( const vec3& plane_normal, const vec3& plane_position,
		const vec3& local_z, carve::math::Matrix& resulting_matrix );

	/** MeshSet and Polyhedron operations */
	void applyTransform( shared_ptr<carve::input::PolyhedronData>& poly_data, const carve::math::Matrix& matrix );

	void applyTranslate( shared_ptr<carve::mesh::MeshSet<3> >& meshset, const vec3& pos, double CARVE_EPSILON );

	void applyTransform( shared_ptr<carve::mesh::MeshSet<3> >& meshset, const carve::math::Matrix& matrix, double CARVE_EPSILON);

	void applyTransform( carve::geom::aabb<3>& aabb, const carve::math::Matrix& matrix );

	/** matrix operations */
	bool computeInverse( const carve::math::Matrix& matrix_a, carve::math::Matrix& matrix_inv, const double eps = 0.01 );

	bool checkMatricesIdentical( const carve::math::Matrix &A, const carve::math::Matrix &B, double tolerance = 0.000001 );

	void removeDuplicates(std::vector<vec3>& loop);

	void removeDuplicates( std::vector<vec2>&	loop );

	void removeDuplicates( std::vector<std::vector<vec2> >&	paths );

	void copyClosedLoopSkipDuplicates( const std::vector<vec2>& loop_in, std::vector<vec2>& loop_out );

	void intersectLoopWithPoints(std::vector<vec3>& loop, std::vector<carve::mesh::Vertex<3> >&  vertices, double eps, size_t maxNumPoints );

	std::pair<vec3, vec3> shortestConnectionSegmentToSegment(const vec3 A, const vec3 B, const vec3 C, const vec3 D, double CARVE_EPSILON);


	bool pointInPolySimple(const std::vector<vec2>& points, const vec2& p, double CARVE_EPSILON);

	bool isEnclosed(const std::vector<vec2>& loop1, const std::vector<vec2>& loop2, double CARVE_EPSILON);

	//\brief: finds the first occurrence of T in vector
	template<typename T, typename U>
	bool findFirstInVector( std::vector<shared_ptr<U> > vec, shared_ptr<T>& ptr )
	{
		typename std::vector<shared_ptr<U> >::iterator it_trim = vec.begin();
		for( ; it_trim != vec.end(); ++it_trim )
		{
			shared_ptr<U> item = *( it_trim );
			ptr = dynamic_pointer_cast<T>( item );
			if( ptr )
			{
				return true;
			}
		}
		return false;
	}

	template<typename T>
	bool allPointersValid( const std::vector<shared_ptr<T> >& vec )
	{
		for( size_t ii = 0; ii < vec.size(); ++ii )
		{
			const shared_ptr<T>& ptr = vec[ii];
			if( !ptr )
			{
				return false;
			}
		}
		return true;
	}


	enum RayTriangleIntersection {
		INTERSECT_EDGE,
		INTERSECT_INTERIOR,
		INTERSECT_NONE
	};


	class aabb {
	public:
		glm::dvec3 pos;
		glm::dvec3 extent;

		aabb();

		aabb(glm::dvec3 _pos, glm::dvec3 _extent);

		bool isEmpty();

		void unionAABB(const aabb& other);
	};
} // end namespace GeomUtils

} // end namespace IFCC
