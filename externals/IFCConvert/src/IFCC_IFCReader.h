#ifndef IFCC_IFCReaderH
#define IFCC_IFCReaderH

#include <IBK_assert.h>
#include <IBK_Path.h>

#include <IBKMK_Polygon3D.h>

#include <ReaderSTEP.h>

#include <ifcpp/IFC4X3/include/IfcSpaceTypeEnum.h>

#include <QCoreApplication>
#include <QStringList>

#include <IBK_NotificationHandler.h>

#include <VICUS_Project.h>

#include <VicIFC_Model.h>

#include "IFCC_GeometryConverter.h"
#include "IFCC_Types.h"
#include "IFCC_Project.h"
#include "IFCC_Site.h"
#include "IFCC_Database.h"
#include "IFCC_Instances.h"
#include "IFCC_BuildingElementsCollector.h"

namespace IFCC {

class ProgressHandler;

/*! Main class for IFC reading and conversion int vicus model.*/
class IFCReader : public QObject {
Q_OBJECT

public:

	enum Name_Id_Type {
		NIT_Space,
		NIT_SpaceBoundary
	};

	/*! Struct contains flags or parameter for controlling conversion.
		It should repair possible IFC problems.
	*/
	struct RepairFlags {
		bool	m_removeDoubledSBs	= false;	///< remove space boundaries which contains the same surface

	};

	/*! Standard constructor. Initializes geometry converter.*/
	IFCReader();

	~IFCReader();

	/*! Clear complete data structure.*/
	void clear();

	/*! Clear all data sets filled by convert function.*/
	void clearConvertData();

	/*! Read the IFC file.
		\param filename Path to the IFC file.
		\param ignoreReadError If true, continue even if read errors occur.
		\param notify Optional notification handler for progress reporting.
	*/
	bool read(const IBK::Path& filename, bool ignoreReadError, IBK::NotificationHandler* notify = nullptr);

	/*! Convert IFC data into internal format similar to SimVicus.
		It needs calling of read before. splitShapeData is called internally.
		When space boundaries exist they are used in order to create component instances.
		Otherwise space boundaries will be created by using a matching algorithm from building components to spaces.
		\param useSpaceBoundaries When true space boundaries will be used if the exist.
		\param notify Optional notification handler for progress reporting.
	*/
	bool convert(bool useSpaceBoundaries, IBK::NotificationHandler* notify = nullptr);

	/*! Return the total number of IFC entities. Call read before use.*/
	int totalNumberOfIFCEntities() const;

	/*! Return the total number of IFC space boundaries. Call read before use.*/
	int numberOfIFCSpaceBoundaries() const;

	/*! Check if the essential IFC objects are present after reading.
		Read must be done before and successful.
	*/
	bool checkEssentialIFCs(QString& errmsg, int& buildings, int& spaces);

	/*! Check if a space contains equal space boundaries.
		\param equalSBs Contain ids of equal space boundaries
		\return Number of qual space boundaries
	*/
	int checkForEqualSpaceBoundaries(std::vector<std::pair<int,int>>& equalSBs) const;

	/*! Check if a subsurface space boundary is contained more than once in construction space boundaries of a space.
		\param res Vector of space ids with corresponding multiple used subsurface space boundary ids
		\return Number of spaces with equal subsurfaces
	*/
	int checkForUniqueSubSurfacesInSpaces(std::vector<std::pair<int,std::vector<int>>>& res) const;

	/*! Check if the surfaces of two spaces are intersected. If this is the case two spaces have a common volume.
		This should never be the case and is an index for a faulty IFC file.
		\return Set of pairs of intersected spaces given by their ids
	*/
	std::set<std::pair<int,int>> checkForIntersectedSpace() const;

	/*! Check if two spaces share the same space boundary.
		\return Set of pairs of intersected spaces given by their ids
	*/
	std::set<std::pair<int,int>> checkForSpaceWithSameSpaceBoundaries() const;

	/*! Check if a component instance is connected to a non existing surface.
	 *  It returns a vector of component instance ids which are wrong.
	 *  Everything is allright in case the vector is empty.
	*/
	std::vector<int> checkForWrongSurfaceIds();

	/*! Count the number of openings which are not related to a space boundary.*/
	int checkForNotRelatedOpenings() const;

	/*! Return state of remove doubled space boundaries flag.*/
	bool removeDoubledSBs() const;

	/*! Set the state of the remove doubled space boundaries flag.
		If true one of the two doubled space boundaries will be removed before the space is saved.
	*/
	void setRemoveDoubledSBs(bool removeDoubledSBs);

	/*! Search for object with given id and type and return its name.*/
	QString nameForId(int id, Name_Id_Type) const;

	/*! Build a VICUS::Project from the current converted data.*/
	VICUS::Project buildVicusProject() const;

	/*! Raw IFC model (all building elements with their original triangulated mesh,
		metadata and property sets), populated during convert() via buildIFCModel().
		Independent of the VICUS conversion; intended for rendering/serialization of
		the original IFC geometry.*/
	const VicIFC::Model& ifcModel() const { return m_ifcModel; }

	/*! Write converted data as vicus file.*/
	void writeXML(const IBK::Path & filename) const;

	/*! Create a VICUS project text based on the current content.*/
	void setVicusProjectText(QString& projectText);

	/*! Return a string list with all messages.*/
	QStringList messages() const;

	/*! Create an return a statistic text.*/
	QStringList statistic() const;

	/*! Return all errors found while converting.*/
	const std::vector<ConvertError>& convertErrors() const;

	/*! Return the name of the current IFC file.*/
	IBK::Path filename() const;

	/*! Return true if the given element type will be used for creating space boundaries by matching with space surfaces.*/
	bool hasElementsForSpaceBoundaries(BuildingElementTypes type) const;

	/*! Add the given element type to the list of types used for creating space boundaries by matching with space surfaces.*/
	void setElementsForSpaceBoundaries(BuildingElementTypes type, bool set);

	/*! Clear the elemnt matching type list.*/
	void clearElementsForSpaceBoundaries() {
		m_convertOptions.m_elementsForSpaceBoundaries.clear();
	}

	/*! Set the type usid for matching space boundaries.
	 *  \li CM_MatchEachConstruction - use each construction and cut space surfaces
	 *  \li CM_MatchOnlyFirstConstruction - use only the matching construction with the highest priority (distance, area, type)
	 *  \li CM_MatchFirstNConstructions - use a given number of construction from the priority list
	 *  \li CM_NoMatching - don't perform a matching. All space boundaries will have missing element type
	 */
	void setConvertMatchingType(ConvertOptions::ConstructionMatching type) {
		m_convertOptions.m_matchingType = type;
	}

	/*! Set additional matching parameter.
		\param constructionFactor Factor which will be multiplied with the construction thickness for maximum matching distance
		\param openingDistance Maximum distance for matching openings to space boundaries
	*/
	void setMatchingDistances(double constructionFactor, double standardWallThickness, double openingDistance);

	/*! Set the flag in order to limit the opening matching only to walls.*/
	void addNoSearchForOpenings(const QSet<BuildingElementTypes>& types);

	/*! Set if the building elements will be written as plain geometry into the vicus file.*/
	void setWritingBuildingElements(bool constructions, bool buildingElements, bool openings, bool other);

	/*! Controls per-category which building elements are additionally exported as
		VICUS::ShadingObject entries in the resulting VICUS project.
		\param construction Walls, slabs, roofs
		\param similar Beams, columns, covering, footing, curtain wall
		\param opening Windows and doors (high face-count, usually noisy)
		\param other Everything else (currently not populated by the reader)
	*/
	void setWriteShadingObjects(bool construction, bool similar, bool opening, bool other);

	/*! Enable/disable the coplanar-face merge pre-pass for the shading export.
		Default on — without it, each mesh face of a building element becomes its
		own shading surface, producing thousands of slivers for real buildings. */
	void setMergeShadingCoplanarFaces(bool enabled);

	/*! Set the minimum values for basic checks.
	 *  \param minimumDistance Used as eps for search for equal or coplanar points
	 *  \param minimumArea Areas smaller than this will not taken into account
	*/
	void setMinimumCheckValues(double minimumDistance, double minimumArea, double polygonEpsilon);

	/*! Set opening simplifying by use of CSG.
	 *  If true a new opening will be calculated by using the substraction of original opening from building element.
	*/
	void setUseCSGForOpenings(bool useCSG);

	/*! Set with which method the surfaces should be written.
	 *  If true a 3D polygon like the old SIM-VICUS is used.
	 *  Otherwise the current method with 2D polygon and plane parameter is used.
	*/
	void setSurfaceWritingMode(bool oldStyle);

	/*! Return the current convert options.
	*/
	const ConvertOptions &convertOptions() const;

	bool							m_hasError;				///< If true an error while reading IFC file was occured
	bool							m_hasWarning;			///< If true an warning while reading IFC file was occured
	std::string						m_errorText;			///< Text of error messages
	std::string						m_warningText;			///< Text of warning messages
	std::string						m_progressText;			///< Progress text
	bool							m_readCompletedSuccessfully;
	bool							m_convertCompletedSuccessfully;

private:
	/*! Create a building model from the given STEP file.*/
	bool loadModelFromSTEPFile( const IBK::Path& filePath, shared_ptr<BuildingModel>& targetModel );

	/*! Helper function for updateing all building elements and collect them according type.
		\param notify Optional notification handler for per-element progress.
	*/
	void updateBuildingElements(IBK::NotificationHandler* notify = nullptr);

	/*! Populate m_ifcModel with one VicIFC::IFCObject per building element,
		carrying its original triangulated mesh, GUID/type/name and property sets.
		Must be called after updateBuildingElements() (needs m_originalMesh filled).*/
	void buildIFCModel();

	/*! Append geometry-less spatial-structure objects (site/building/storey) to the IFC
		model and set element parent ids (storey containment, or containing wall for
		windows/doors). Must run after updateStoreys (m_site populated). */
	void updateIFCModelTopology();

	IBK::Path						m_filename;				///< IFC file
	std::shared_ptr<BuildingModel>	m_model;				///< IFC model created from file
	RepairFlags						m_repairFlags;			///< Contains all flags and parameter for IFC repair
	GeometryConverter				m_geometryConverter;	///< Geometry converter for converting local to global coordinates.
	/*! Vector for shapes of building element entities with type.*/
	objectShapeTypeVector_t			m_elementEntitesShape;
	/*! Map with GUID as key and corresponding shape of spatial entity (except site, building, storey and space).*/
	objectShapeGUIDMap_t			m_spatialEntitesShape;
	/*! Map with GUID as key and corresponding shape of space entity.*/
	objectShapeGUIDMap_t			m_spaceEntitesShape;
	/*! Map with GUID as key and corresponding shape of entity which are others than element, opening or spatial structure.*/
	objectShapeGUIDMap_t			m_unknownEntitesShape;
	/*! Map with GUID as key and corresponding shape of building entity.*/
	objectShapeGUIDMap_t			m_buildingsShape;
	/*! Map with GUID as key and corresponding shape of storey entity.*/
	objectShapeGUIDMap_t			m_storeysShape;
	/*! Map with GUID as key and corresponding shape of opening entity.*/
	objectShapeGUIDMap_t			m_openingsShape;
	/*! Map with GUID as key and corresponding shape of external spatial entity.*/
	objectShapeGUIDMap_t			m_externalSpatialShapes;
	/*! Map with GUID as key and corresponding shape of spatial zone entity.*/
	objectShapeGUIDMap_t			m_spatialZoneShapes;
	/*! Pointer to site shape.*/
	shared_ptr<ProductShapeData>	m_siteShape;

	/*! Collection of building elements.*/
	BuildingElementsCollector		m_buildingElements;

	/*! Vector of opening elements.
		Openings should have connections to the corresponding opening structure and the construction element which contains the opening.
	*/
	std::vector<Opening>								m_openings;
	/*! Ifc project. It can contain buildings in case no site is present.*/
	Project												m_project;
	/*! Building site. It can contain geometric objects for surrounding.*/
	Site												m_site;
	/*! Handler class for all databases (constructions, materials, windows, glazings, components).*/
	Database											m_database;
	/*! Handler class for all component instances. Such a instance is a connection of components and surfaces from spaces.*/
	Instances											m_instances;
	/*! Raw IFC model built by buildIFCModel(): all building elements with original mesh geometry and metadata.*/
	VicIFC::Model										m_ifcModel;
	/*! Vector of errors while converting.*/
	std::vector<ConvertError>							m_convertErrors;
	/*! Elements which are used for creating space boundaries.*/
	ConvertOptions										m_convertOptions;


	/*! Function for collecting messages from IFC reading process (error, warning, progress).*/
	static void messageTarget( void* obj_ptr, shared_ptr<StatusCallback::Message> t );

	/*! Call the geometry converter which converts IFC objects from local to global coordinate system.
		From this a vector of shape data will be cerated. Then the data are split with regard to their type.
		While this process all shape vectors will be filled.
		This function must be called before convert or write can be performed.
	*/
	void splitShapeData();

	/*! It evaluates the object and their type from element shape vector from the given GUID.
		\param guid IFC element GUID as string
		\param res Resulting pair of element pointer and its type.
	*/
	bool typeByGuid(const std::string& guid, std::pair<BuildingElementTypes,std::shared_ptr<ProductShapeData>>& res);

	/*! Check openings not connected to an opening element and try to match them.
		\param notify Optional notification handler for per-opening progress.
	*/
	void checkAndMatchOpeningsToConstructions(IBK::NotificationHandler* notify = nullptr);

	bool		m_useSpaceBoundaries = true;

	/*! Active sub-range progress relay used by messageTarget() to forward
		StatusCallback PROGRESS_VALUE events from ifcplusplus (ReaderSTEP parser,
		GeometryConverter) to the parent IBK::NotificationHandler. Set during the
		long-running parse and convertGeometry() calls so the GUI thread keeps
		pumping QApplication::processEvents() and the progress dialog stays alive.
		Pointer to a stack-allocated ProgressHandler in read()/convert(); nullptr
		outside those windows. */
	ProgressHandler*	m_currentSubProgress = nullptr;

};

} // end namespace

#endif // IFCC_IFCREADER_H
