#ifndef IFCC_PolygonRoundTripCheckH
#define IFCC_PolygonRoundTripCheckH

#include <string>

#include <IBKMK_Polygon3D.h>

namespace IFCC {

/*! Predicts whether a Polygon3D will survive the VICUS XML round-trip.

	VICUS::Polygon2D::writeXML and IBKMK::Polygon3D::writeXML serialize the 2D
	polyline vertices and the offset/normal/localX vectors with default
	std::ostream precision (= 6 significant digits). On read, the data is fed
	through Polygon2D::setVertexes (which runs eliminateCollinearPoints with
	TOLERANCE = 1e-4 m = 0.1 mm) and the four-argument Polygon3D ctor (which
	requires the first 2D vertex to be exactly (0,0), unit-length normal/localX,
	and orthogonality with nearly_equal<4> tolerance = 1e-4).

	If the predicate fails, a Logger entry is emitted that names the specific
	failure mode (Polygon2D invalid, first vertex shifted, unit-length deviation,
	orthogonality deviation) along with the relevant numeric quantities, so the
	root cause can be diagnosed from the import log.

	\param poly3D       Polygon to validate.
	\param surfaceName  Used only for log identification.
	\param surfaceId    Used only for log identification.
	\return true if the polygon survives a write/read round-trip; false otherwise
			(with a detailed Logger entry already written).
*/
bool willSurviveXmlRoundTrip(const IBKMK::Polygon3D& poly3D,
							 const std::string& surfaceName,
							 unsigned int surfaceId);

} // namespace IFCC

#endif // IFCC_PolygonRoundTripCheckH
