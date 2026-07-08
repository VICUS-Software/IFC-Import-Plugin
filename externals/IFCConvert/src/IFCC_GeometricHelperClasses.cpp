#include "IFCC_GeometricHelperClasses.h"

#include <IBK_math.h>
#include <IBK_assert.h>

#include <fstream>


namespace IFCC {

namespace {

bool rotateVector(const IBKMK::Vector3D& vect, PlaneNormal::PolygonPlane plane, IBKMK::Vector2D& res) {
	switch(plane) {
		case PlaneNormal::XYPlane: {
			res.m_x = vect.m_x;
			res.m_y = vect.m_y;
			return true;
		}
		case PlaneNormal::XZPlane: {
			res.m_x = vect.m_x;
			res.m_y = vect.m_z;
			return true;
		}
		case PlaneNormal::YZPlane: {
			res.m_x = vect.m_y;
			res.m_y = vect.m_z;
			return true;
		}
		case PlaneNormal::SlopedPlane: {
			return false;
		}
		case PlaneNormal::Unknown: {
			return false;
		}
	}
	return false;
}

auto vectorDirection(IBKMK::Vector3D vect) -> double {
	vect.normalize();
	int zeros = 0;
	int neg = 0;
	int pos = 0;
	if(IBK::near_zero(vect.m_x))
		++zeros;
	else if(vect.m_x > 0)
		++pos;
	else ++neg;
	if(IBK::near_zero(vect.m_y))
		++zeros;
	else if(vect.m_y > 0)
		++pos;
	else ++neg;
	if(IBK::near_zero(vect.m_z))
		++zeros;
	else if(vect.m_z > 0)
		++pos;
	else ++neg;
	if(pos > neg) return 1.0;
	if(zeros == 3) return 0.0;
	return -1.0;
}

bool rotateVectorBack(const IBKMK::Vector2D& vect, PlaneNormal::PolygonPlane plane, double dist, IBKMK::Vector3D& res) {
	switch(plane) {
		case PlaneNormal::XYPlane: {
			res.m_x = vect.m_x;
			res.m_y = vect.m_y;
			res.m_z = dist;
			return true;
		}
		case PlaneNormal::XZPlane: {
			res.m_x = vect.m_x;
			res.m_y = dist;
			res.m_z = vect.m_y;
			return true;
		}
		case PlaneNormal::YZPlane: {
			res.m_x = dist;
			res.m_y = vect.m_x;
			res.m_z = vect.m_y;
			return true;
		}
		case PlaneNormal::SlopedPlane: {
			return false;
		}
		case PlaneNormal::Unknown: {
			return false;
		}
	}
	return false;
}

bool smallerVectZero(const IBKMK::Vector3D& vect) {
	if (vect.m_x < 0) {
		return true;
	}
	if (vect.m_x > 0) {
		return false;
	}
	if (vect.m_y < 0) {
		return true;
	}
	if (vect.m_y > 0) {
		return false;
	}
	if (vect.m_z < 0) {
		return true;
	}
	if (vect.m_z > 0) {
		return false;
	}
	return false;
}

} // end anonymous namespace


/*! Matrix for rotation of 3D geometry objects (3x3).*/
class RotationMatrix {
public:
	/*! Matrix data with different access possibilities.*/
	union {
		double m_m[3][3];	///< As matrix
		double m_v[9];		///< As vector
		/*! as single variables.*/
		struct {
			double m_11, m_12, m_13;
			double m_21, m_22, m_23;
			double m_31, m_32, m_33;
		};
	};

	/*! Constructor with single variables.*/
	RotationMatrix(double __11, double __21, double __31, double __12, double __22,
				   double __32, double __13, double __23, double __33);

	/*! Constructor with matrix data.*/
	RotationMatrix(double m[3][3]);

	/*! Constructor with plain vector.*/
	RotationMatrix(double v[9]);

	/*! Standard constructor. Create a identity matrix.*/
	RotationMatrix();

	/*! Return a LU factorized version.*/
	RotationMatrix lu() const;

	/*! Backsolving with given result vector of size 3.
		Can only be used with an LU factorized matrix.*/
	void backsolve(double * b) const;

	/*! Create inverse of rotation matrix.*/
	bool inverse(RotationMatrix& invMat) const;

	/*! Equality operator.*/
	friend bool operator==(const RotationMatrix& A, const RotationMatrix& B);

	/*! Non-equal operator.*/
	friend bool operator!=(const RotationMatrix& A, const RotationMatrix& B);

	/*! Product of rotation matrix with 3D vector.*/
	friend IBKMK::Vector3D operator*(const RotationMatrix& A, const IBKMK::Vector3D& b);

	/*! Product of 3D vector with rotation matrix.*/
	friend IBKMK::Vector3D& operator*=(IBKMK::Vector3D& b, const RotationMatrix& A);

	/*! Product of two matrices.*/
	friend RotationMatrix operator*(const RotationMatrix& A, const RotationMatrix& B);

private:
	/*! Helper function for determinant.*/
	double determinantOfMinor( int theRowHeightY, int theColumnWidthX ) const;

	/*! Determinant of matrix. Helper function for inverse.*/
	double determinant() const;
};


// Rotation matrix 3x3

RotationMatrix::RotationMatrix(double __11, double __21, double __31, double __12, double __22,
		double __32, double __13, double __23, double __33)  :
	m_11(__11),
	m_12(__12),
	m_13(__13),
	m_21(__21),
	m_22(__22),
	m_23(__23),
	m_31(__31),
	m_32(__32),
	m_33(__33)
{}

RotationMatrix::RotationMatrix(double m[3][3]) {
	std::memcpy(m_m, m, sizeof(m_m));
}

RotationMatrix::RotationMatrix(double v[9]) {
	std::memcpy(m_v, v, sizeof(m_v));
}

RotationMatrix::RotationMatrix() :
  m_11(1.00),
  m_12(0.00),
  m_13(0.00),
  m_21(0.00),
  m_22(1.00),
  m_23(0.00),
  m_31(0.00),
  m_32(0.00),
  m_33(1.00)
{}

RotationMatrix RotationMatrix::lu() const {
	RotationMatrix res(*this);

	if (res.m_v[0] == 0)
		return RotationMatrix();

	/* column 0 */
	res.m_v[1] /= res.m_v[0];
	res.m_v[2] /= res.m_v[0];

	/* column 1 */
	res.m_v[4] -= res.m_v[1]*res.m_v[3];
	if (res.m_v[4] == 0)
		return RotationMatrix();

	res.m_v[5] -= res.m_v[2]*res.m_v[3];
	res.m_v[5] /= res.m_v[4];

	/* column 2 */
	res.m_v[7] -= res.m_v[1]*res.m_v[6];
	res.m_v[8] -= res.m_v[2]*res.m_v[6] + res.m_v[5]*res.m_v[7];
	if (res.m_v[8] == 0)
		return RotationMatrix();

	return res;
}

/*! Can only be used with an LU factorized matrix.*/
void RotationMatrix::backsolve(double * b) const {
	b[1] -= m_v[1]*b[0];
	b[2] -= m_v[2]*b[0] + m_v[5]*b[1];

	/* backward elimination */
	b[2] /= m_v[8];
	b[1] -= m_v[7]*b[2];
	b[1] /= m_v[4];
	b[0] -= m_v[6]*b[2] + m_v[3]*b[1];
	b[0] /= m_v[0];
}

double RotationMatrix::determinantOfMinor( int theRowHeightY, int theColumnWidthX ) const {
	int x1 = theColumnWidthX == 0 ? 1 : 0;  /* always either 0 or 1 */
	int x2 = theColumnWidthX == 2 ? 1 : 2;  /* always either 1 or 2 */
	int y1 = theRowHeightY   == 0 ? 1 : 0;  /* always either 0 or 1 */
	int y2 = theRowHeightY   == 2 ? 1 : 2;  /* always either 1 or 2 */

	return ( m_m[y1][x1]  * m_m[y2][x2] )
			-  ( m_m[y1][x2]  *  m_m[y2][x1] );
}

double RotationMatrix::determinant() const {
	return ( m_m[0][0]      *  determinantOfMinor( 0, 0 ) )
			-  ( m_m[0][1]  *  determinantOfMinor( 0, 1 ) )
			+  ( m_m[0][2]  *  determinantOfMinor( 0, 2 ) );
}

bool RotationMatrix::inverse(RotationMatrix& invMat) const {
	double det = determinant();

	/* Arbitrary for now.  This should be something nicer... */
	if ( std::fabs(det) < 1e-2 )   {
		invMat = RotationMatrix();
		return false;
	}

	double oneOverDeterminant = 1.0 / det;

	for (size_t y = 0;  y < 3;  y ++ )
		for (size_t x = 0;  x < 3;  x ++   ) {
			/* Rule is inverse = 1/det * minor of the TRANSPOSE matrix.  *
		 * Note (y,x) becomes (x,y) INTENTIONALLY here!              */
			invMat.m_m[y][x]
					= determinantOfMinor( x, y ) * oneOverDeterminant;

			/* (y0,x1)  (y1,x0)  (y1,x2)  and (y2,x1)  all need to be negated. */
			if( 1 == ((x + y) % 2) )
				invMat.m_m[y][x] = - invMat.m_m[y][x];
		}

	return true;
}

bool operator==(const RotationMatrix& A, const RotationMatrix& B) {
	for (size_t i = 0; i < 9; ++i) {
		if (!IBK::near_equal(A.m_v[i], B.m_v[i])) {
			return false;
		}
	}
	return true;
}

bool operator!=(const RotationMatrix& A, const RotationMatrix& B) {
	return !(A == B);
}

IBKMK::Vector3D operator*(const RotationMatrix& A, const IBKMK::Vector3D& b) {
	return IBKMK::Vector3D(A.m_11 * b.m_x + A.m_21 * b.m_y + A.m_31 * b.m_z,
						   A.m_12 * b.m_x + A.m_22 * b.m_y + A.m_32 * b.m_z,
						   A.m_13 * b.m_x + A.m_23 * b.m_y + A.m_33 * b.m_z);
}

IBKMK::Vector3D& operator*=(IBKMK::Vector3D& b, const RotationMatrix& A) {
	b = A * b;
	return b;
}

RotationMatrix operator*(const RotationMatrix& A, const RotationMatrix& B) {
	RotationMatrix c;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			c.m_m[i][j] = 0.0;
			for (int k = 0; k < 3; k++) {
				c.m_m[i][j] += A.m_m[k][j] * B.m_m[i][k];
			}
		}
	}
	return c;
}

// Class for plane in Hesse normal form

PlaneHesseNormal::PlaneHesseNormal() :
	m_d(0)
{
}

PlaneHesseNormal::PlaneHesseNormal(const carve::geom::plane<3>& plane) :
	m_n0(plane.N.x,plane.N.y,plane.N.z),
	m_d(plane.d)
{
}

PlaneHesseNormal::PlaneHesseNormal(const std::vector<IBKMK::Vector3D>& polygon) :
	m_d(0)
{
	getPlane(polygon);
}

bool PlaneHesseNormal::getPlane(const std::vector<IBKMK::Vector3D>& polygon) {

	if (polygon.size() < 3) {
		return false;
	}

	// get centroid
	IBKMK::Vector3D C;
	for(const IBKMK::Vector3D& vert : polygon) {
		C += vert;
	}
	C = C * (1.0/polygon.size());


	IBKMK::Vector3D n;
	if (polygon.size() == 3) {
		n = (polygon[1] - polygon[0]).crossProduct(polygon[2] - polygon[0]);
	}
	else {
		size_t N = polygon.size();

		n = (polygon[N - 1] - C).crossProduct(polygon[0] - C);
		if (smallerVectZero(n)) {
			n = n * -1;
		}
		for (size_t i = 1; i < polygon.size(); ++i) {
			IBKMK::Vector3D v = (polygon[i] - C).crossProduct(polygon[i - 1] - C);
			if (smallerVectZero(v)) {
				v = v * -1.0;
			}
			n += v;
		}
	}

	double l = n.magnitude();

	if (l == 0.0) {
		n.m_x = 1.0;
		n.m_y = 0.0;
		n.m_z = 0.0;
	}
	else {
		n.normalize();
	}

	m_n0 = n;
	m_d = n.scalarProduct(C) * -1.0;
	return true;
}


PlaneNormal::PlaneNormal() :
	m_valid(false),
	m_polygonPlane(Unknown),
	m_distance(0),
	m_rotationMatrix(new RotationMatrix),
	m_rotationMatrixInv(new RotationMatrix)
{
}

PlaneNormal::PlaneNormal(const PlaneNormal& src) :
	m_valid(src.m_valid),
	m_polygonPlane(src.m_polygonPlane),
	m_distance(src.m_distance),
	m_pos(src.m_pos),
	m_rot_pos(src.m_rot_pos),
	m_lx(src.m_lx),
	m_ly(src.m_ly),
	m_lz(src.m_lz),
	m_rotationMatrix(new RotationMatrix(*src.m_rotationMatrix)),
	m_rotationMatrixInv(new RotationMatrix(*src.m_rotationMatrixInv))
{
}

PlaneNormal& PlaneNormal::operator=(const PlaneNormal& src) {
	m_valid = src.m_valid;
	m_polygonPlane = src.m_polygonPlane;
	m_distance = src.m_distance;
	m_pos = src.m_pos;
	m_rot_pos = src.m_rot_pos;
	m_lx = src.m_lx;
	m_ly = src.m_ly;
	m_lz = src.m_lz;
	m_rotationMatrix.reset(new RotationMatrix(*src.m_rotationMatrix));
	m_rotationMatrixInv.reset(new RotationMatrix(*src.m_rotationMatrixInv));
	return *this;
}

PlaneNormal::~PlaneNormal() = default;

PlaneNormal::PlaneNormal(const PlaneHesseNormal& plane, const IBKMK::Vector3D& pos) :
	m_valid(true),
	m_polygonPlane(Unknown),
	m_distance(0),
	m_pos(pos),
	m_rot_pos(pos),
	m_rotationMatrixInv(new RotationMatrix)
{
	setPlaneProperties(plane);

	IBKMK::Vector3D A(1,1,1);
	if(IBK::near_zero(plane.m_n0.m_x)) {
		if(IBK::near_zero(plane.m_n0.m_y)) {
			// only z
			IBK_ASSERT(!IBK::near_zero(plane.m_n0.m_z));
			A.m_z = m_distance / plane.m_n0.m_z;
		}
		// z and y
		else {
			A.m_y = (m_distance - plane.m_n0.m_z) / plane.m_n0.m_y;
		}
	}
	else if(IBK::near_zero(plane.m_n0.m_y)) {
		// only x
		if(IBK::near_zero(plane.m_n0.m_z)) {
			A.m_x = m_distance / plane.m_n0.m_x;
		}
		// x and z
		else {
			A.m_x = (m_distance - plane.m_n0.m_y) / plane.m_n0.m_x;
		}
	}
	// x and y
	else {
		A.m_x = (m_distance - plane.m_n0.m_y - plane.m_n0.m_z) / plane.m_n0.m_x;
	}
	m_lx = A - pos;
	m_ly = m_lz.crossProduct(m_lx);

	m_lx.normalize();
	m_lz.normalize();
	m_ly.normalize();

	m_rotationMatrix.reset(new RotationMatrix(m_lx.m_x, m_ly.m_x, m_lz.m_x,
											  m_lx.m_y, m_ly.m_y, m_lz.m_y,
											  m_lx.m_z, m_ly.m_z, m_lz.m_z));

	bool inverseValid = m_rotationMatrix->inverse(*m_rotationMatrixInv);
	if(!inverseValid)
		m_valid = false;

	m_rot_pos = *m_rotationMatrixInv * m_pos * -1;
}

PlaneNormal::PlaneNormal(const PlaneHesseNormal& plane, const std::vector<IBKMK::Vector3D>& polygon) :
	m_valid(true),
	m_polygonPlane(Unknown),
	m_distance(0),
	m_rotationMatrixInv(new RotationMatrix)
{
	IBK_ASSERT(polygon.size() > 2);

	setPlaneProperties(plane);
	IBK_ASSERT(m_polygonPlane != Unknown);

	if(m_polygonPlane == SlopedPlane) {
		IBKMK::Vector3D p1;
		IBKMK::Vector3D p2;
		IBKMK::Vector3D startVect;
		bool found = false;
		for(size_t i=0; i<polygon.size()-1; ++i) {
			p1 = polygon[i];
			p2 = polygon[i+1];
			startVect = p2 - p1;
			startVect.normalize();
			if(!IBK::near_zero(startVect.m_x) || !IBK::near_zero(startVect.m_y)) {
				found = true;
				break;
			}
		}
		m_valid = found;
		m_pos = p1;

		m_lx = p2 - p1;
		m_lx.normalize();
		if(IBK::near_zero(m_lx.m_x)) {
			m_ly = p2 - p1;
			m_lx = m_lz.crossProduct(m_ly);
		}
		else {
			m_lx = p2 - p1;
			m_ly = m_lz.crossProduct(m_lx);
		}

		m_lx.normalize();
		m_ly.normalize();
		m_lz.normalize();
	}
	else if(m_polygonPlane == XYPlane) {
		m_lx = IBKMK::Vector3D(1,0,0);
		m_ly = IBKMK::Vector3D(0,1,0);
		m_lz = IBKMK::Vector3D(0,0,1);
	}
	else if(m_polygonPlane == XZPlane) {
		m_lx = IBKMK::Vector3D(1,0,0);
		m_ly = IBKMK::Vector3D(0,0,1);
		m_lz = IBKMK::Vector3D(0,-1,0);
	}
	else if(m_polygonPlane == YZPlane) {
		m_lx = IBKMK::Vector3D(0,0,1);
		m_ly = IBKMK::Vector3D(0,1,0);
		m_lz = IBKMK::Vector3D(-1,0,0);
	}
	else {
		// should never happen
	}

	m_rotationMatrix.reset(new RotationMatrix(m_lx.m_x, m_ly.m_x, m_lz.m_x,
											  m_lx.m_y, m_ly.m_y, m_lz.m_y,
											  m_lx.m_z, m_ly.m_z, m_lz.m_z));

	bool inverseValid = m_rotationMatrix->inverse(*m_rotationMatrixInv);
	if(!inverseValid)
		m_valid = false;

	m_rot_pos = *m_rotationMatrixInv * m_pos * -1;
}

PlaneNormal::PlaneNormal(const std::vector<IBKMK::Vector3D>& polygon) :
	m_valid(true),
	m_polygonPlane(Unknown),
	m_distance(0),
	m_rotationMatrixInv(new RotationMatrix)
{
	// Degenerate polygons (e.g. empty/collapsed SB fragments from broken IFC models) must
	// not crash the import — IBK_ASSERT is a no-op in release builds, so check explicitly
	// and bail out with m_valid == false (callers check validity).
	if(polygon.size() < 3) {
		m_valid = false;
		m_rotationMatrix.reset(new RotationMatrix);
		return;
	}

	m_pos = polygon[0];
	m_lx = polygon[1] - polygon[0];
	m_ly =  polygon.back() - polygon[0];
	m_lz = m_lx.crossProduct(m_ly);
	if (m_lz.magnitude() < 1e-9) {
		// collapsed spanning vectors — continuing would divide by ~0 below
		m_valid = false;
		m_rotationMatrix.reset(new RotationMatrix);
		return;
	}

	IBKMK::Vector3D n0 = m_lz * (1.0 / m_lz.magnitude());
	double d = m_pos.scalarProduct(n0);
	double fact = vectorDirection(n0);
	if(IBK::near_zero(fact)) {
		m_valid = false;
		m_rotationMatrix.reset(new RotationMatrix);
		return;
	}

	m_distance = d * fact * -1.0;

	m_polygonPlane = SlopedPlane;

	int sameDirectionCount = 0;

	// now process all other points and generate their normal vectors as well
	for (unsigned int i=1; i<polygon.size(); ++i) {
		IBKMK::Vector3D vx = polygon[(i+1) % polygon.size()] - polygon[i];
		IBKMK::Vector3D vy = polygon[i-1] - polygon[i];
		IBKMK::Vector3D vn;
		vx.crossProduct(vy, vn);
		if (vn.magnitude() < 1e-9)
			continue; // invalid vertex input

		vn.normalize();
		// adding reference normal to current vertexes normal and checking magnitude works
		if ((vn + m_lz).magnitude() > 1) // can be 0 or 2, so comparing against 1 is good even for rounding errors
			++sameDirectionCount;
		else
			--sameDirectionCount;
	}

	if (sameDirectionCount < 0) {
		// invert our normal vector
		m_lz *= -1;
	}

	m_ly = m_lz.crossProduct(m_lx);
	m_lx.normalize();
	m_ly.normalize();
	m_lz.normalize();

	m_rotationMatrix.reset(new RotationMatrix(m_lx.m_x, m_ly.m_x, m_lz.m_x,
											  m_lx.m_y, m_ly.m_y, m_lz.m_y,
											  m_lx.m_z, m_ly.m_z, m_lz.m_z));

	bool inverseValid = m_rotationMatrix->inverse(*m_rotationMatrixInv);
	if(!inverseValid)
		m_valid = false;

	m_rot_pos = *m_rotationMatrixInv * m_pos * -1;
}

IBKMK::Vector2D PlaneNormal::convert3DPoint(const IBKMK::Vector3D& point) const {

	if(m_polygonPlane != Unknown && m_polygonPlane != SlopedPlane) {
		IBKMK::Vector2D result;
		if(rotateVector(point, m_polygonPlane, result)) {
			IBKMK::Vector2D trans;
			rotateVector(m_pos, m_polygonPlane, trans);
			return result - trans;
		}
	}

	IBKMK::Vector3D tmp = *m_rotationMatrixInv * point;
	tmp = tmp + m_rot_pos;

	return IBKMK::Vector2D(tmp.m_x, tmp.m_y);
}

IBKMK::Vector3D PlaneNormal::convert3DPointInv(const IBKMK::Vector2D& point) const {

	if(m_polygonPlane != Unknown && m_polygonPlane != SlopedPlane) {
		IBKMK::Vector2D trans;
		rotateVector(m_pos, m_polygonPlane, trans);
		IBKMK::Vector2D tmp = point + trans;
		IBKMK::Vector3D result;
		if(rotateVectorBack(tmp, m_polygonPlane, m_distance, result)) {
			return result;
		}
	}

	IBKMK::Vector3D res = IBKMK::Vector3D(point.m_x, point.m_y, 0) - m_rot_pos;
	res = *m_rotationMatrix * res;

	return res;
}


void PlaneNormal::setPlaneProperties(const PlaneHesseNormal& plane) {
	double d = IBK::near_zero(plane.m_d) ? 1 : plane.m_d;
	double fact = vectorDirection(plane.m_n0);
	IBK_ASSERT(!IBK::near_zero(fact));

	m_distance = plane.m_d * fact * -1.0;
	IBKMK::Vector3D n = plane.m_n0 * d;
	m_lz = fact < 0 ? -1 * n : n;
	m_lz.normalize();

	if((IBK::near_equal(m_lz.m_x,1) || IBK::near_equal(m_lz.m_x,-1)) && IBK::near_zero(m_lz.m_y) && IBK::near_zero(m_lz.m_z))
		m_polygonPlane = YZPlane;
	else if(IBK::near_zero(m_lz.m_x) && (IBK::near_equal(m_lz.m_y,1) || IBK::near_equal(m_lz.m_y,-1)) && IBK::near_zero(m_lz.m_z))
		m_polygonPlane = XZPlane;
	else if(IBK::near_zero(m_lz.m_x) && IBK::near_zero(m_lz.m_y) && (IBK::near_equal(m_lz.m_z,1) || IBK::near_equal(m_lz.m_z,-1)))
		m_polygonPlane = XYPlane;
	else
		m_polygonPlane = SlopedPlane;
}

} // namespace IFCC
