#include "IFCC_PolygonRoundTripCheck.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include <IBKMK_Polygon2D.h>
#include <IBKMK_Vector2D.h>
#include <IBKMK_Vector3D.h>

#include "IFCC_Logger.h"

namespace IFCC {

namespace {

// Round a single double through std::ostream precision matching what
// VICUS::Polygon2D::writeXML stores in the XML (16 significant digits, which
// makes the round-trip bit-identical for normal IEEE-754 doubles).
double roundtripDouble(double v) {
	std::stringstream ss;
	ss << std::setprecision(16) << v;
	double out = 0.0;
	ss >> out;
	return out;
}

IBKMK::Vector2D roundtripVec2(const IBKMK::Vector2D& v) {
	return IBKMK::Vector2D(roundtripDouble(v.m_x), roundtripDouble(v.m_y));
}

} // anonymous namespace


bool willSurviveXmlRoundTrip(const IBKMK::Polygon3D& poly3D,
							 const std::string& surfaceName,
							 unsigned int surfaceId)
{
	const IBKMK::Vector3D originalNormal = poly3D.normal();
	const IBKMK::Vector3D originalLocalX = poly3D.localX();
	const std::vector<IBKMK::Vector2D>& originalVerts = poly3D.polyline().vertexes();

	// 1) Round-trip offset/normal/localX through Vector3D::toString(prec)/fromString();
	//    must match the precision used by VICUS::Polygon3D::writeXML (16 sig digits).
	IBKMK::Vector3D rtOffset, rtNormal, rtLocalX;
	try {
		rtOffset = IBKMK::Vector3D::fromString(poly3D.offset().toString(16));
		rtNormal = IBKMK::Vector3D::fromString(poly3D.normal().toString(16));
		rtLocalX = IBKMK::Vector3D::fromString(poly3D.localX().toString(16));
	}
	catch (...) {
		Logger::instance() << "Warning: Surface '" << surfaceName << "' (id " << surfaceId
			<< ") round-trip failed [vec-serialize]: offset/normal/localX threw on fromString()"
			<< " - skipping";
		return false;
	}

	const double normalMagSqOrig = originalNormal.magnitudeSquared();
	const double localXMagSqOrig = originalLocalX.magnitudeSquared();
	const double dotOrig         = originalNormal.scalarProduct(originalLocalX);
	const double normalMagSqRt   = rtNormal.magnitudeSquared();
	const double localXMagSqRt   = rtLocalX.magnitudeSquared();
	const double dotRt           = rtNormal.scalarProduct(rtLocalX);

	// 2) Round-trip 2D vertices through default-precision stringstream
	std::vector<IBKMK::Vector2D> rtVerts(originalVerts.size());
	for (size_t i = 0; i < originalVerts.size(); ++i)
		rtVerts[i] = roundtripVec2(originalVerts[i]);

	// 3) Polygon2D ctor runs eliminateCollinearPoints with TOLERANCE = 1e-4 (0.1 mm)
	//    and may drop the (0,0) first vertex if it becomes collinear after rounding.
	IBKMK::Polygon2D rtPoly2D(rtVerts);
	const size_t vertsBefore = rtVerts.size();
	const size_t vertsAfter  = rtPoly2D.vertexes().size();

	if (!rtPoly2D.isValid()) {
		Logger::instance() << "Warning: Surface '" << surfaceName << "' (id " << surfaceId
			<< ") round-trip failed [polyline]: Polygon2D invalid after collinear-elim"
			<< " (verts " << vertsBefore << "->" << vertsAfter
			<< "; either <3 verts left or non-simple/self-intersecting)"
			<< " - skipping";
		return false;
	}

	// 4) The four-arg Polygon3D ctor requires polyline.vertexes()[0] == (0,0).
	//    eliminateCollinearPoints can have removed the original (0,0) anchor.
	const IBKMK::Vector2D firstAfter = rtPoly2D.vertexes()[0];
	if (firstAfter != IBKMK::Vector2D(0.0, 0.0)) {
		const double shift = std::sqrt(firstAfter.m_x*firstAfter.m_x +
									   firstAfter.m_y*firstAfter.m_y);
		Logger::instance() << "Warning: Surface '" << surfaceName << "' (id " << surfaceId
			<< ") round-trip failed [first-vertex]: anchor (0,0) dropped by collinear-elim"
			<< " (verts " << vertsBefore << "->" << vertsAfter
			<< ", new first=(" << firstAfter.m_x << "," << firstAfter.m_y
			<< ") shift=" << shift << " m)"
			<< " - skipping";
		return false;
	}

	// 5) Polygon3D ctor calls setRotation(normal, localX), which throws if any
	//    of: |normal|^2 != 1, |localX|^2 != 1, normal·localX != 0 (tol 1e-4).
	IBKMK::Polygon3D rtPoly3D(rtPoly2D, rtOffset, rtNormal, rtLocalX);
	if (!rtPoly3D.isValid()) {
		const double devNormalMag = std::abs(normalMagSqRt - 1.0);
		const double devLocalXMag = std::abs(localXMagSqRt - 1.0);
		const double devOrtho     = std::abs(dotRt);
		Logger::instance() << "Warning: Surface '" << surfaceName << "' (id " << surfaceId
			<< ") round-trip failed [rotation]: Polygon3D rejected by setRotation (tol=1e-4);"
			<< " |N|^2 orig=" << normalMagSqOrig << " rt=" << normalMagSqRt
			<< " dev=" << devNormalMag
			<< "; |X|^2 orig=" << localXMagSqOrig << " rt=" << localXMagSqRt
			<< " dev=" << devLocalXMag
			<< "; N.X orig=" << dotOrig << " rt=" << dotRt
			<< " dev=" << devOrtho
			<< " - skipping";
		return false;
	}

	return true;
}

} // namespace IFCC
