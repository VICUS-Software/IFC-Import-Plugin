#ifndef IFCC_OpeningH
#define IFCC_OpeningH


#include <ifcpp/IFC4X3/include/IfcFeatureElementSubtraction.h>


//#include "IFCC_SpaceBoundary.h"
#include "IFCC_Surface.h"
#include "IFCC_EntityBase.h"

namespace IFCC {

class SpaceBoundary;
class Space;

/*! Class represents a opening.
	This mainly a geometric object which have connections to a construction element and an opening element.
*/
class Opening : public EntityBase
{
public:
	/*! Standard constructor.
		\param id Unique id for using in project.
	*/
	explicit Opening(int id);

	/*! Initialise opening from a IFC element object.
		It set a name and guid.
		\param ifcElement Original IFC element which can represent a opening.
	*/
	bool set(std::shared_ptr<IFC4X3::IfcFeatureElementSubtraction> ifcElement);

	/*! Get and transform geometry and fill surface vector.
		\param Shape data of a opening.
	*/
	void update(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors);

	/*! Return all surfaces of this opening.*/
	const std::vector<Surface>& surfaces() const;

	/*! Repair authoring-tool-inflated opening bodies (WSHH: void extrusions padded
		by exactly ±5m, producing 10m boxes through the building). Detects a body
		whose extent along its extrusion axis is implausible for a wall void, finds
		the INTERIOR vertex stations (the true wall slab survives as intermediate
		vertices), and injects the two cross-section faces there as ST_ProbableSide
		while demoting the inflated faces to ST_UnProbableSide — the per-face opening
		matcher then works with the true window planes. No-op for plausible bodies.
	*/
	void repairOversizedBody();

	/*! Return the original triangulated opening solid (from the IFC-to-carve conversion).*/
	const meshVector_t& originalMesh() const {
		return m_originalMesh;
	}

	/*! Return all surfaces created by CSG intersection with construction of this opening.*/
	const std::vector<Surface>& surfacesCSGElement() const;

	/*! Return all surfaces created by CSG intersection with space of this opening.*/
	const std::vector<Surface> & surfacesCSGSpace() const;

	/*! Return GUID of original IFC object.*/
	std::string guid() const;

	/*! Add the given id to the opening element ids vector.
		\param id Id of an opening element (window or door) which is connected to this opening
	*/
	void addOpeningElementId(int id);

	/*! Add the given id to the containing element ids vector.
		\param id Id of a construction element (wall, roof or slab) which is connected to this opening
	*/
	void addContainingElementId(int id);

	/*! Add all ids from the containing element ids vector to the end of the given one.*/
	void insertContainingElementId(std::vector<int>& other) const;

	/*! Add a connected space boundary. An internal door/window connects two rooms
		and collects one opening space boundary per room side.*/
	void addSpaceBoundary(std::shared_ptr<SpaceBoundary> sb);

	/*! Return true if the opening is connected to at least one space boundary.*/
	bool hasSpaceBoundary() const;

	/*! Return true if the opening already has a space boundary in the space with the given GUID.
		Used by the per-space matching so a second room sharing the same wall can still
		attach this opening, while the same room never attaches it twice.*/
	bool hasSpaceBoundaryInSpace(const std::string& spaceGuid) const;

	/*! All space boundaries this opening is attached to (one per room side). */
	const std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries() const { return m_spaceBoundaries; }

	/*! Get the vector of opening construction ids connected to this opening.*/
	const std::vector<int>& openingElementIds() const;

	bool isConnectedToOpeningElement() const {
		return !m_openingElementIds.empty();
	}

	/*! Try to find surfaces are parallel or intersecting building element surfaces.
	 *  Set a surface type based on the results.
	*/
	void checkSurfaceType(const BuildingElement& element, double eps);

	/*! Create a 3D intersection with opening and given building element.
	 *  Store the resulting surfaces in m_surfacesCSG.
	*/
	void createCSGSurfaces(const BuildingElement& element, double eps);

	/*! Create a 3D intersection with opening and given building element.
	 *  Store the resulting surfaces in m_surfacesCSG.
	*/
	void createCSGSurfaces(const Space& space, double eps);

	/*! Map store the surface indices which are connected to a space given by ID.
		Map key is id of space.
		First value is index if space surface.
		Second value is index of opening surface.
	*/
	std::map<int,std::vector<std::pair<size_t,size_t>>>	m_spaceSurfaceConnection;

private:

	/*! Transfor geometric shape data from local into global coordinate system.
		Will be called from update.
	*/
	void transform(std::shared_ptr<ProductShapeData> productShape);

	/*! Take geometry from shape data and convert it into a list of surfaces.*/
	void fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors);

	std::string						m_guid;						///< GUID of original IFC object.

	std::vector<Surface>			m_surfaces;					///< Surrfaces of the opening

	/*! Contains surface which are created by the following way:
	 *  \li create a substraction body from opening and corresponding building element
	 *  \li select only surfaces which are at the same plane as building element surfaces
	*/
	std::vector<Surface>			m_surfacesCSGElement;

	/*! Contains surface which are created by the following way:
	 *  \li create a substraction body from opening and corresponding space
	 *  \li select only surfaces which are at the same plane as space surfaces
	*/
	std::vector<Surface>			m_surfacesCSGSpace;

	/*! Vector of ids of opening elements (window or door) which are used from this opening (should only be one).*/
	std::vector<int>				m_openingElementIds;

	/*! Vector of ids of construction elements (wall, roof or slab) which contains this opening (should only be one).*/
	std::vector<int>				m_containedInElementIds;

	/*! Connected space boundaries — one per room side seeing this opening.*/
	std::vector<std::shared_ptr<SpaceBoundary>>	m_spaceBoundaries;

	/*! Original 3D mesh from conversion IFC to carve.*/
	meshVector_t					m_originalMesh;
};

} // namespace IFCC

#endif // IFCC_OpeningH
