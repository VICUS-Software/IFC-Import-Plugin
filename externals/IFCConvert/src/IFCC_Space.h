#ifndef IFCC_SpaceH
#define IFCC_SpaceH

#include <ifcpp/IFC4X3/include/IfcSpaceTypeEnum.h>
#include <ifcpp/IFC4X3/include/IfcSpace.h>

#include <IBKMK_Vector3D.h>

#include <tinyxml.h>

#include <VICUS_Room.h>

#include <carve/poly.hpp>

//#include "IFCC_BuildingElement.h"
#include "IFCC_Helper.h"
#include "IFCC_Surface.h"
#include "IFCC_EntityBase.h"
#include "IFCC_SpaceBoundary.h"
#include "IFCC_Opening.h"
#include "IFCC_BuildingElementsCollector.h"
#include "IFCC_ConvertOptions.h"

namespace IFCC {

/*! Class represents a space (room) in a building.
	It contains also all space boundaries as connection from space surface to building or opening elements.
*/
class Space : public EntityBase
{
public:

	/*! Type of matching algorithm for openings.*/
	enum OpeningMatchingType {
		OMT_SamePoints,					///< Opening and space boundary has identical points
		OMT_WallThicknessIntersection,	///< A intersction is found inside the distance of the corresponding construction thickness
		OMT_NoMatching
	};

	enum CompositionType {
		CT_Complex,
		CT_Element,
		CT_Partial,
		CT_Unknown
	};

	/*! Contains result of the opening matching functions.*/
	struct OpeningMatching {
		/*! Default constructor. Create a non-valid object.*/
		OpeningMatching() :
			m_type(OMT_NoMatching),
			m_surfaceIndex(-1),
			m_subSurfaceIndex(-1)
		{}

		/*! Standard constructor.
			\param type Type of used matching algorithm
			\param si Index of found surface
			\param subi Index of matching subsurface
		*/
		OpeningMatching(OpeningMatchingType type, size_t si, size_t subi) :
			m_type(type),
			m_surfaceIndex(si),
			m_subSurfaceIndex(subi)
		{}

		OpeningMatchingType m_type;				///< Type of used matching algorithm
		size_t				m_surfaceIndex;		///< Index of found surface
		size_t				m_subSurfaceIndex;	///< Index of matching subsurface
	};

	/*! Contains result of space boundary matching functions.*/
	struct OpeningConstructionMatching {
		/*! Default constructor. Create a non-valid object.*/
		OpeningConstructionMatching() :
			m_constructionSurfaceIndex(-1),
			m_openingIndex(-1),
			m_openingSurfaceIndex(-1)
		{}

		/*! Standard constructor.
			\param constructionSurfaceIndex Index of surface in current construction
			\param m_openingIndex Index of opening
			\param m_openingSurfaceIndex Index of surface in the opening
		*/
		OpeningConstructionMatching(int constructionSurfaceIndex, int spaceBoundaryIndex, int spaceBoundarySurfaceIndex) :
			m_constructionSurfaceIndex(constructionSurfaceIndex),
			m_openingIndex(spaceBoundaryIndex),
			m_openingSurfaceIndex(spaceBoundarySurfaceIndex)
		{}

		/*! Return true if all indices are valid.*/
		bool isValid() const {
			if(m_constructionSurfaceIndex < 0)
				return false;
			if(m_openingIndex < 0)
				return false;
			if(m_openingSurfaceIndex < 0)
				return false;
			return true;
		}

		int m_constructionSurfaceIndex;		///< Index of surface in current construction
		int m_openingIndex;			///< Index of opening
		int m_openingSurfaceIndex;	///< Index of surface in opening
	};

	/*! Standard constructor.
		\param id Unique id for using in project.
	*/
	explicit Space(int id);

	/*! Initialise space from a IFC building element object.
		It set a name, long name and space type..
		Space boundaries will be created if the IFC data include some.
		\param ifcSpace Original IFC space element
	*/
	bool set(std::shared_ptr<IFC4X3::IfcSpace> ifcSpace, std::vector<ConvertError>& errors);

	/*! Updates the space geometry from shape by calling fetchGeometry and transform.
		Set the transformation matrix.
		\param productShape Shape data of the space created from geometry converter.
	*/
	void update(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors);

	/*! Updates the type and content (surfaces) of the space boundaries. Also the connection to building elements will be set.
		If the IFC model doesn't contain space boundaries the function try to evaluate these from construction elements and openings.
		\param shapes Vector of element shaps with type
		\param unit_converter Unit converter from geometry converter
		\param constructionElements Vector of all construction elements (wall, roof, slab)
		\param openingElements Vector of all opening elements (window, door)
		\param openings Vector of openings with connections to opening elements and construction elements
		\return If false no space boundaries with connected building elements can be found (\sa m_spaceBoundaryErrors).
		In case of evaluated space boundaries for openings a connection will be set in the corresponding opening.
	*/
	bool updateSpaceBoundaries(const objectShapeTypeVector_t& shapes,
							   shared_ptr<UnitConverter>& unit_converter,
							   const BuildingElementsCollector& buildingElements,
							   std::vector<Opening>& openings,
							   bool useSpaceBoundaries,
							   std::vector<ConvertError>& errors,
							   const ConvertOptions& convertOptions);

	/*! Phase 1: Create construction space boundaries (thread-safe, no shared mutable state).
		Can be called in parallel for different spaces.
		\param buildingElements Collector of all building elements (read-only).
		\param errors Vector of errors while conversion (per-thread, merge afterwards).
		\param convertOptions Options for conversion (read-only).
		\return Vector of created space boundaries for this space.
	*/
	std::vector<std::shared_ptr<SpaceBoundary>> createConstructionSpaceBoundaries(
		const BuildingElementsCollector& buildingElements,
		std::vector<ConvertError>& errors,
		const ConvertOptions& convertOptions);

	/*! Phase 2: Match openings to space boundaries and finalize (requires sequential access).
		Must be called sequentially since openings vector is shared across spaces.
		\param spaceBoundaries Space boundaries from Phase 1 to finalize.
		\param buildingElements Collector of all building elements.
		\param openings Vector of all openings (shared mutable state).
		\param errors Vector of errors while conversion.
		\param convertOptions Options for conversion.
		\return False if no space boundaries could be found.
	*/
	bool finalizeConstructionSpaceBoundaries(
		std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
		const BuildingElementsCollector& buildingElements,
		std::vector<Opening>& openings,
		std::vector<ConvertError>& errors,
		const ConvertOptions& convertOptions);

	/*! Link Openings to the opening space boundaries attached to this space's
		construction SBs (IFC space-boundary path). Only links when the resulting
		SubSurface would survive the write-time validation in getVicusSurface, so
		unlinked openings remain available for the cross-space fallback.
		MUST be called sequentially (writes into the shared openings vector) —
		called from BuildingStorey::updateSpaces AFTER the parallel per-space phase.
		\param buildingElements Collector of all building elements (read-only).
		\param openings Vector of all openings (shared mutable state).
	*/
	void linkOpeningsToSpaceBoundaries(const BuildingElementsCollector& buildingElements,
									   std::vector<Opening>& openings);

	/*! Return all space boundaries of this space.*/
	const std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries() const;

	/*! Candidate returned by findBestOpeningMatch — represents a potential attachment
		of one opening to a space boundary in this space, without committing it.
	*/
	struct OpeningMatchCandidate {
		std::shared_ptr<SpaceBoundary>    parentSB;
		std::shared_ptr<BuildingElement>  openingElem;
		Surface                           mergedSurface;
		double                            area = -1.0;
		/*! Plane distance opening body <-> SB [m]. Tie-breaker between parallel
			wall layers (Gipsputz vs. Gipskarton) that both intersect the full
			window outline: the layer closest to the opening wins. */
		double                            dist = 1e20;
	};

	/*! Compare two opening-match candidates: larger intersection area wins, but
		ranking areas are clamped to the opening element's own area (broken opening
		bodies with duplicate faces produce doubled merge areas), and at (near) equal
		rank area the smaller plane distance wins. Used by the per-space selection
		AND the cross-space fallback in Building::updateStoreys so both apply
		identical semantics.
		\return True if cand should replace best.
	*/
	static bool isBetterOpeningMatch(const OpeningMatchCandidate& cand, const OpeningMatchCandidate& best);

	/*! Find the best-area match for the given opening among this space's own
		construction SBs, without committing. Returns an empty candidate (area < 0)
		if no SB in this space produces a valid match. Used by the cross-space
		fallback at Building level.
		\param ignoreContainedOpeningsFilter When true, try every construction SB
		regardless of whether the SB's building element lists the opening in
		m_containedOpenings. Used by the cross-space fallback — geometry alone decides
		which room the opening belongs in.
		\param allowCoplanarAccept When true, if the 2D intersection of the opening
		face and the SB polygon is empty but both share the same plane, accept the
		match using the opening's own polygon. This is a last-resort heuristic for
		curtainwall-like walls where the room's SB is a partial slice of the full
		wall face; it must only be enabled for the global Pass B fallback so per-space
		matching can't commit an opening in the wrong (first-processed) room.
	*/
	OpeningMatchCandidate findBestOpeningMatch(Opening& opening,
											   const BuildingElementsCollector& buildingElements,
											   const ConvertOptions& convertOptions,
											   bool ignoreContainedOpeningsFilter = false,
											   bool allowCoplanarAccept = false,
											   std::vector<OpeningMatchCandidate>* allCandidates = nullptr) const;

	/*! From all per-SB candidates of one opening, select the split pieces belonging
		to the winning candidate: coplanar with it, pairwise non-overlapping, combined
		area capped at 1.2x the window/door outline. Returns the pieces INCLUDING the
		winner (front). Openings frequently span several coplanar wall fragments —
		committing only the best fragment cuts a partial hole (user report: half the
		arch window lost). */
	static std::vector<OpeningMatchCandidate> collectSplitPieces(const OpeningMatchCandidate& best,
																 const std::vector<OpeningMatchCandidate>& all,
																 const ConvertOptions& convertOptions);

	/*! Commit a previously-computed match by creating a new opening SpaceBoundary,
		attaching it to the given parent SB, and appending it to this space's SBs.
	*/
	void commitOpeningMatch(Opening& opening,
							const OpeningMatchCandidate& candidate,
							const ConvertOptions& convertOptions);

	/*! If space boundaries with the same surface exist one of whem will be removed.
	*/
	void removeDublicatedSpaceBoundaries(const ConvertOptions& convertOptions);

	/*! Check if subsurfaces are used more than once.
		\return vector of space boundary ids which are used more than once as subsurface
	*/
	std::vector<int> checkUniqueSubSurfaces() const;

	/*! Check if some space boundaries have the same surface.
		\param equalSBs vector of ids of the equal space boundaries
	*/
	void checkForEqualSpaceBoundaries(std::vector<std::pair<int,int>>& equalSBs, const ConvertOptions& convertOptions) const;

	/*! Check if the current space is intersected to the other one.*/
	bool isIntersected(const Space& other, const ConvertOptions& convertOptions) const;

	/*! Create a VICUS::Room from this space.
		The returned object contains all surfaces from space boundaries.
	*/
	VICUS::Room getVicusObject(const ConvertOptions& options) const;

	/*! Write the space in vicus xml format including space boundaries.
		\param parent Parent xml node
	*/
	TiXmlElement * writeXML(TiXmlElement * parent, const ConvertOptions& convertOptions) const;

	/*! Return true if the space contains a space boundary with the given GUID.*/
	bool hasSpaceBoundary(const std::string& guid) const;

	/*! Return true if the two spaces share one space boundary.*/
	bool shareSameSpaceBoundary(const Space& space) const;

	std::string									m_longName;			///< More detailed name of the space
	/*! IFC space type. It can be:
		\li ENUM_SPACE - Any space not falling into another category.
		\li ENUM_PARKING - A space dedication for use as a parking spot
		\li ENUM_GFA - Gross Floor Area - a specific kind of space for each building story that includes all net area and construction area
		\li ENUM_INTERNAL - not defined yet
		\li ENUM_EXTERNAL - not defined yet
		\li ENUM_USERDEFINED - user defined space type
		\li ENUM_NOTDEFINED - undefined space type
	*/
	IFC4X3::IfcSpaceTypeEnum::IfcSpaceTypeEnumEnum		m_spaceType;

	CompositionType										m_compositionType = CT_Unknown;

	/*! Some remarkes to the space. Can contain notes from space boundary evaluation.*/
	std::string											m_notes;

	meshVector_t meshSets() const;

	std::vector<Surface> surfacesOrg() const;

private:

	/*! Transforms the space geometry by using transformation matrix from productShape.
		It transforms all coordinates from local system into global system.
	*/
	void transform(std::shared_ptr<ProductShapeData> productShape);

	/*! Get the geometry from the product shape.
		It fills the surface vector m_surfaces.
	*/
	void fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors);

	/*! Is called from evaluateSpaceBoundaryFromIFC.
		It set the the space boundary type and the id of the connected building element.
		\param shapes Vector of element shaps with type
		\param buildingElements Vector of all building elements (wall, roof, slab, window, door etc.)
	*/
	void evaluateSpaceBoundaryTypes(const objectShapeTypeVector_t& shapes,
									 const BuildingElementsCollector& buildingElements,
									 const ConvertOptions& convertOptions);

	/*! Is called from evaluateSpaceBoundaryFromIFC.
		It get geometry for space boundaries and fill their temporary surface vector.
		\param unit_converter Unit converter from geometry converter
		\param errors Vector of all conversion errors which occures here
	*/
	bool evaluateSpaceBoundaryGeometry(shared_ptr<UnitConverter>& unit_converter,
									   std::vector<ConvertError>& errors, const ConvertOptions& convertOptions);

	/*! Is called from updateSpaceBoundaries in case space boundaries could be evaluated from IFC model.
		It set the the space boundary type and the id of the connected building element (evaluateSpaceBoundaryTypes).
		It get geometry for space boundaries and fill their temporary surface vector (evaluateSpaceBoundaryGeometry).
		It splits all space boundaries which have more than one surface and adds the result to the space vector.
		It set the connections between construction space boundaries and opening space boundaries.
		\param shapes Vector of element shaps with type
		\param buildingElements Vector of all building elements (wall, roof, slab, window, door etc.)
		\param unit_converter Unit converter from geometry converter
		\param errors Vector of all conversion errors which occures here
	*/
	bool evaluateSpaceBoundaryFromIFC(const objectShapeTypeVector_t& shapes,
									  const BuildingElementsCollector& buildingElements,
									  shared_ptr<UnitConverter>& unit_converter,
									  std::vector<ConvertError>& errors,
									  const ConvertOptions& convertOptions);

	/*! Re-anchor IFC-authored space boundaries onto the space's own solid shell
		(m_surfacesOrg) and fill uncovered shell parts with "Missing" SBs.
		Heals plane offsets up to convertOptions.m_shellSnapTolerance and closes
		the room volume even for incomplete authored SB sets. Only called from
		evaluateSpaceBoundaryFromIFC (the construction-matching path derives its
		SBs from the shell already).
	*/
	void anchorSpaceBoundariesToShell(const ConvertOptions& convertOptions);

	/*! Is called from updateSpaceBoundaries in case IFC model doesn't contain space boundaries.
		It try to evaluate space boundaries from construction elements and openings.
		\param buildingElements Vector of all building elements (wall, roof, slab, window, door etc.)
		\param openings Vector of all openings
		\param errors Vector of all conversion errors which occures here
	*/
	bool evaluateSpaceBoundariesFromConstruction(const BuildingElementsCollector& buildingElements,
												 std::vector<Opening>& openings,
												 std::vector<ConvertError>& errors,
												 const ConvertOptions& convertOptions);

	/*! Try to find space boundaries for construction elements by test of surface properties (parallel, distance, intersections).
		Function will be called from evaluateSpaceBoundaries.
		\param constructionElements Vector for all construction elements with own surfaces
		\return Vector of evaluated space boundaries
	*/
	std::vector<std::shared_ptr<SpaceBoundary>> createSpaceBoundaries( const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																	   const ConvertOptions& convertOptions);

	/*! Try to find space boundaries for construction elements by test of surface properties (parallel, distance, intersections).
		Function will be called from evaluateSpaceBoundaries.
		\param constructionElements Vector for all construction elements with own surfaces
		\return Vector of evaluated space boundaries
	*/
	std::vector<std::shared_ptr<SpaceBoundary>> createSpaceBoundaries_2( const BuildingElementsCollector& buildingElements, std::vector<ConvertError>& errors,
																		 const ConvertOptions& convertOptions);

	/*! Try to find space boundaries for opening elements based on openings.
		\param spaceBoundaries Result vector for adding new space boundaries
		\param openingElements Vector of opening elements (window or door).
		\param openings Vector of all openings
	*/
	void createSpaceBoundariesForOpeningsFromOpenings(std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
													  const BuildingElementsCollector& buildingElements,
													  const std::vector<Opening>& openings, std::vector<ConvertError>& errors);

	/*! Create opening space boundaries by matching openings to existing space boundaries.*/
	void createSpaceBoundariesForOpeningsFromSpaceBoundaries(std::vector<std::shared_ptr<SpaceBoundary>>& spaceBoundaries,
															 const BuildingElementsCollector& buildingElements,
															 std::vector<Opening>& openings, std::vector<ConvertError>& errors,
															 const ConvertOptions& convertOptions);

	std::vector<std::shared_ptr<SpaceBoundary>>				m_spaceBoundaries;	///< Space boundaries of the space
	carve::math::Matrix										m_transformMatrix;	///< Matrix for geometry transformation from local to global coordinates
	std::vector<Surface>									m_surfacesOrg;		///< Original surfaces from the IFC model converted into global coordinates
	meshVector_t											m_meshSets;
	std::vector<std::string>								m_spaceBoundaryGUIDs;
	std::map<std::string,std::map<std::string,Property>>	m_properties;
};

} // namespace IFCC

#endif // IFCC_SpaceH
