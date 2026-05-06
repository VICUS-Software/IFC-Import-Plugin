#ifndef IFCC_GeometryInputDataH
#define IFCC_GeometryInputDataH

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

#include <vector>
#include <string>

#include <ifcpp/geometry/AppearanceData.h>
#include <ifcpp/model/BasicTypes.h>
#include <ifcpp/model/BuildingObject.h>
#include <ifcpp/IFC4X3/include/IfcObjectPlacement.h>
#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>
#include <ifcpp/IFC4X3/include/IfcRepresentation.h>
#include <ifcpp/geometry/IncludeCarveHeaders.h>

//namespace GeomDebugDump
//{
//	static void dumpMeshset(carve::mesh::MeshSet<3>* meshset, const glm::vec4& color, bool move_offset = true);
//	static void dumpMeshsetOpenEdges(const shared_ptr<carve::mesh::MeshSet<3> >& meshset, const glm::vec4& colorInput, bool checkZeroAreaFaces, bool move_offset);
//}

#include "IFCC_GeomUtils.h"
#include "IFCC_GeometrySettings.h"

namespace IFCC {


class TextItemData
{
public:
	std::string m_text;
	carve::math::Matrix m_text_position;
};

inline void premultMatrix( const carve::math::Matrix& matrix_to_append, carve::math::Matrix& target_matrix )
{
	target_matrix = matrix_to_append*target_matrix;
}
class PolyInputCache3D;

/**
*\brief Class TransformData: holds a matrix of a coordinate system and a pointer to the corresponding IFC placement entity
*/
class TransformData
{
public:
	TransformData(){}

	TransformData( const carve::math::Matrix& matrix, weak_ptr<BuildingEntity>& placement_entity, int placement_id );

	TransformData( const shared_ptr<TransformData>& other );

	bool isSameTransform( shared_ptr<TransformData>& transform_data );

	carve::math::Matrix			m_matrix;
	weak_ptr<BuildingEntity>	m_placement_entity;
	int							m_placement_tag = -1;
};

bool checkPolyhedronData( const shared_ptr<carve::input::PolyhedronData>& poly_data, double minFaceArea );

bool fixPolyhedronData(const shared_ptr<carve::input::PolyhedronData>& poly_data, double minFaceArea );

bool reverseFacesInPolyhedronData(const shared_ptr<carve::input::PolyhedronData>& poly_data);

class PolyInputCache3D
{
public:
	PolyInputCache3D( double epsMergePoints = -1 );

	size_t addPointZ( const vec3& pt, std::map<double, size_t>& map_z );

	size_t addPointYZ( const vec3& pt, std::map<double, std::map<double, size_t> >& map_yz );

	size_t addPoint(const vec3& pt);

	void clearAllData();

	double m_eps = 0;
	shared_ptr<carve::input::PolyhedronData> m_poly_data;
	std::map<double, std::map<double, std::map<double, size_t> > > m_existing_vertices_coords;
};

class RepresentationData;
class ProductShapeData;

/**
*\brief Class ItemShapeData: holds input data of one IFC geometric representation item.
* Parent-child relationship of ItemShapeData, RepresentationData, ProductShapeData:
*        ...
*          |-> ProductShapeData [1...n]
*                   |-> ProductShapeData [1...n]
*                           |-> RepresentationData [1...n]
*                                     |-> ItemShapeData [1...n]
*/
class ItemShapeData
{
public:
	ItemShapeData(){}
	~ItemShapeData(){}

	std::vector<shared_ptr<carve::input::PolylineSetData> > m_polylines;
	std::vector<shared_ptr<carve::mesh::MeshSet<3> > >		m_meshsets;
	std::vector<shared_ptr<carve::mesh::MeshSet<3> > >		m_meshsets_open;
	std::vector<shared_ptr<AppearanceData> >				m_vec_item_appearances;
	std::vector<shared_ptr<TextItemData> >					m_vec_text_literals;
	weak_ptr<RepresentationData>							m_parent_representation;  // Pointer to representation object that this item belongs to
	shared_ptr<IFC4X3::IfcRepresentationItem>				m_ifc_item;
	std::vector<shared_ptr<carve::input::VertexData> >	m_vertex_points;

public:
	bool isEmpty();

	void addOpenOrClosedPolyhedron( const shared_ptr<carve::input::PolyhedronData>& poly_data, double CARVE_EPSILON );

	void addOpenPolyhedron( const shared_ptr<carve::input::PolyhedronData>& poly_data, double CARVE_EPSILON );

	bool addClosedPolyhedron(const shared_ptr<carve::input::PolyhedronData>& poly_data, GeomProcessingParams& params);

	void addPoint( const vec3& point );

	void applyTransformToItem( const shared_ptr<TransformData>& transform, bool matrix_identity_checked = false );

	void applyTransformToItem( const carve::math::Matrix& mat, double CARVE_EPSILON, bool matrix_identity_checked = false );

	shared_ptr<ItemShapeData> getItemShapeDataDeepCopy();

	/** copies the content of other instance and adds it to own content */
	void addItemData( const shared_ptr<ItemShapeData>& other );

	const std::vector<shared_ptr<carve::input::VertexData> >& getVertexPoints() { return m_vertex_points; }

	void computeBoundingBox( carve::geom::aabb<3>& bbox ) const;
};

class RepresentationData
{
public:
	RepresentationData() {}
	~RepresentationData(){}

	weak_ptr<IFC4X3::IfcRepresentation>				m_ifc_representation;
	std::vector<shared_ptr<ItemShapeData> >			m_vec_item_data;
	std::vector<shared_ptr<AppearanceData> >		m_vec_representation_appearances;
	std::string										m_representation_identifier;
	std::string										m_representation_type;
	weak_ptr<ProductShapeData>						m_parent_product;  // Pointer to product object that this representation belongs to

	shared_ptr<RepresentationData> getRepresentationDataDeepCopy();

	void addChildItem( shared_ptr<ItemShapeData>& item_data, shared_ptr<RepresentationData>& ptr_self );

	void appendRepresentationData( shared_ptr<RepresentationData>& other, shared_ptr<RepresentationData>& ptr_self );

	void addAppearance( shared_ptr<AppearanceData>& appearance );

	void clearAppearanceData();

	void clearAll();

	void applyTransformToRepresentation( const carve::math::Matrix& matrix, bool matrix_identity_checked = false );

	void computeBoundingBox( carve::geom::aabb<3>& bbox ) const;
};

class ProductShapeData
{
public:
	std::string m_entity_guid;
	weak_ptr<IFC4X3::IfcObjectDefinition>				m_ifc_object_definition;
	weak_ptr<IFC4X3::IfcObjectPlacement>				m_object_placement;
	std::vector<shared_ptr<RepresentationData> >		m_vec_representations;
	bool												m_added_to_spatial_structure = false;
	weak_ptr<ProductShapeData>							m_parent;
	std::vector<shared_ptr<TransformData> >				m_vec_transforms;
	std::vector<shared_ptr<ProductShapeData> >			m_vec_children;
	std::vector<shared_ptr<AppearanceData> >			m_vec_product_appearances;
	/*! IFCC-side guard: set to true once an IFCC::* class has applied the
		getTransform() composed matrix to this shape's representations.
		Prevents the placement chain from being applied multiple times when a
		shape is reached via more than one processing path — e.g. an
		IfcBuildingElementPart is an aggregate child of its parent wall
		(transformed there via BuildingElement::getShapeOfParts) AND a standalone
		similar BuildingElement (would otherwise be transformed again in its own
		update()). */
	bool												m_transformAppliedByIFCC = false;

	ProductShapeData() {}
	ProductShapeData( std::string entity_guid ) : m_entity_guid(entity_guid) { }

	const std::vector<shared_ptr<ProductShapeData> >& getChildren() { return m_vec_children; }

	shared_ptr<ProductShapeData> getDeepCopy();

	void addAppearance( shared_ptr<AppearanceData>& appearance );

	void clearAppearanceData();

	void clearMeshGeometry();

	void clearAll();

	bool isContainedInParentsList( shared_ptr<ProductShapeData>& product_data_check );

	void addChildProduct( shared_ptr<ProductShapeData>& add_child, shared_ptr<ProductShapeData>& ptr_self );

	/**
	* \brief method getTransform: Computes the transformation matrix, that puts the geometry of this product into global coordinates
	* All transformation matrices of all parent coordinate systems are multiplied.
	*/
	carve::math::Matrix getTransform();

	carve::math::Matrix getRelativeTransform(const shared_ptr<ProductShapeData>& other);

	void addTransform( shared_ptr<TransformData>& transform_data );

	void applyTransformToProduct( const carve::math::Matrix& matrix, bool matrix_identity_checked, bool applyToChildren );

	const std::vector<shared_ptr<AppearanceData> >& getAppearances() { return m_vec_product_appearances; }

	bool isEmpty( bool check_also_children ) const;

	bool hasGeometricRepresentation( bool includeChildren = true ) const;
};


} // end namespace

#endif // IFCC_GeometryInputDataH
