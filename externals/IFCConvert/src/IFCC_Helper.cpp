#include "IFCC_Helper.h"

#include <locale>
#include <codecvt>
#include <fstream>

#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcWall.h>
#include <ifcpp/IFC4X3/include/IfcBeam.h>
#include <ifcpp/IFC4X3/include/IfcChimney.h>
#include <ifcpp/IFC4X3/include/IfcColumn.h>
#include <ifcpp/IFC4X3/include/IfcCovering.h>
#include <ifcpp/IFC4X3/include/IfcCurtainWall.h>
#include <ifcpp/IFC4X3/include/IfcDoor.h>
#include <ifcpp/IFC4X3/include/IfcFooting.h>
#include <ifcpp/IFC4X3/include/IfcMember.h>
#include <ifcpp/IFC4X3/include/IfcPile.h>
#include <ifcpp/IFC4X3/include/IfcPlate.h>
#include <ifcpp/IFC4X3/include/IfcRailing.h>
#include <ifcpp/IFC4X3/include/IfcRamp.h>
#include <ifcpp/IFC4X3/include/IfcRampFlight.h>
#include <ifcpp/IFC4X3/include/IfcRoof.h>
#include <ifcpp/IFC4X3/include/IfcShadingDevice.h>
#include <ifcpp/IFC4X3/include/IfcSlab.h>
#include <ifcpp/IFC4X3/include/IfcStair.h>
#include <ifcpp/IFC4X3/include/IfcStairFlight.h>
#include <ifcpp/IFC4X3/include/IfcWall.h>
#include <ifcpp/IFC4X3/include/IfcCivilElement.h>
#include <ifcpp/IFC4X3/include/IfcDistributionElement.h>
#include <ifcpp/IFC4X3/include/IfcElementAssembly.h>
#include <ifcpp/IFC4X3/include/IfcElementComponent.h>
#include <ifcpp/IFC4X3/include/IfcFurnishingElement.h>
#include <ifcpp/IFC4X3/include/IfcGeographicElement.h>
#include <ifcpp/IFC4X3/include/IfcTransportElement.h>
#include <ifcpp/IFC4X3/include/IfcVirtualElement.h>
#include <ifcpp/IFC4X3/include/IfcExternalSpatialElement.h>
#include <ifcpp/IFC4X3/include/IfcSpatialZone.h>
#include <ifcpp/IFC4X3/include/IfcWindow.h>
#include <ifcpp/IFC4X3/include/IfcFeatureElement.h>

#include <ifcpp/IFC4X3/include/IfcConnectionCurveGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionPointGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionPointEccentricity.h>
#include <ifcpp/IFC4X3/include/IfcConnectionSurfaceGeometry.h>
#include <ifcpp/IFC4X3/include/IfcConnectionVolumeGeometry.h>
#include <ifcpp/IFC4X3/include/IfcFaceBasedSurfaceModel.h>
#include <ifcpp/IFC4X3/include/IfcFaceSurface.h>
#include <ifcpp/IFC4X3/include/IfcSurface.h>

#include <carve/mesh_simplify.hpp>

#include <Carve/src/include/carve/carve.hpp>

#include <IBK_math.h>
#include <IBK_assert.h>

#include "IFCC_MeshUtils.h"
#include "IFCC_Surface.h"
#include "IFCC_RepresentationConverter.h"
#include "IFCC_Clippertools.h"
#include "IFCC_IfcStringDecoder.h"

namespace IFCC {

std::atomic<int> GUID_maker::m_guid{1};


std::string ws2s(const std::wstring& wstr) {
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}

std::string label2s(const std::shared_ptr<IfcLabel>& label) {
	if(label != nullptr)
		return decodeStepIfcString(label->m_value);
	return std::string();
}

std::string text2s(const std::shared_ptr<IfcText>& label) {
	if(label != nullptr)
		return decodeStepIfcString(label->m_value);
	return std::string();
}

std::string name2s(const std::shared_ptr<IfcIdentifier>& text) {
	if(text != nullptr)
		return decodeStepIfcString(text->m_value);
	return std::string();
}

static std::vector<IBKMK::Vector3D> polygonFromVertices(const std::vector<carve::mesh::Vertex<3>* >& vertices) {
	std::vector<IBKMK::Vector3D> poly(vertices.size());
	for(size_t vi=0; vi<vertices.size(); ++vi) {
		double x = vertices[vi]->v.x;
		double y = vertices[vi]->v.y;
		double z = vertices[vi]->v.z;
		poly[vi].set(x,y,z);
	}
	return poly;
}

void convert(const carve::mesh::MeshSet<3>& meshSet, std::vector<std::vector<std::vector<IBKMK::Vector3D>>>&  polyvect) {
	const carve::mesh::MeshSet<3>& currMesh = meshSet;
	int mCount = currMesh.meshes.size();
	for(int mi=0; mi<mCount; ++mi) {
		polyvect.push_back(std::vector<std::vector<IBKMK::Vector3D>>());
		carve::mesh::MeshSet<3>::mesh_t* mesh = currMesh.meshes[mi];
		int faCount = mesh->faces.size();
		for(int fi = 0; fi<faCount; ++fi) {
			carve::mesh::Face<3>* face = mesh->faces[fi];
			if( face->edge == nullptr)
				continue;
			std::vector<carve::mesh::Vertex<3>* > vertices;
			face->getVertices(vertices);
			int numVert = vertices.size();
			if(numVert == 0)
				continue;

			std::vector<IBKMK::Vector3D> poly = polygonFromVertices(vertices);
			cleanPolygon(poly);
			std::vector<polygon3D_t> polysSimple = simplifyPolygon(poly);
			if(polysSimple.empty()) {
				// this can only happe if the original polygon is not valid
				polyvect.back().push_back(poly);
			}
			else {
				for(const auto& p : polysSimple) {
					polyvect.back().push_back(p);
				}
			}
		}
	}
}

void meshDump(const carve::mesh::MeshSet<3>& meshSet, const std::string& name, const IBK::Path& filename) {
	std::ofstream file(filename.c_str(), std::ios::app);
	if(!file.is_open())
		return;

	file << "\n" << name << "\n";
	const carve::mesh::MeshSet<3>& currMesh = meshSet;
	int mCount = currMesh.meshes.size();
	for(int mi=0; mi<mCount; ++mi) {
		file << "mesh: " << mi << "\n";
		carve::mesh::MeshSet<3>::mesh_t* mesh = currMesh.meshes[mi];
		int faCount = mesh->faces.size();
		for(int fi = 0; fi<faCount; ++fi) {
			carve::mesh::Face<3>* face = mesh->faces[fi];
			std::vector<carve::mesh::Vertex<3>* > vertices;
			face->getVertices(vertices);
			int numVert = vertices.size();
			if(numVert == 3)
				file << "dreieck(";
			else if(numVert == 4)
				file << "viereck(";
			else
				file << "polygon(";
			for(int vi=0; vi<numVert; ++vi) {
				double x = vertices[vi]->v.x;
				double y = vertices[vi]->v.y;
				double z = vertices[vi]->v.z;
				if(vi>0)
					file << " ";
				file << x << "|" << y << "|" << z;
			}
			file << ")\n";
		}
	}
	file << "end: " << name << "\n\n";
}


void faceDump(carve::mesh::Face<3>* face, const std::string& name, const IBK::Path& filename) {
	std::vector<std::vector<std::vector<IBKMK::Vector3D>>> data;
	std::ofstream file(filename.c_str(), std::ios::app);
	if(file.is_open()) {
		file << "\n" << name <<"\n";
		std::vector<carve::mesh::Vertex<3>* > vertices;
		face->getVertices(vertices);
		int numVert = vertices.size();
		for( size_t i=0; i<numVert; ++i) {
			if(i>0)
				file << " ";
			file << vertices[i]->v.x << "|" << vertices[i]->v.y << "|" << vertices[i]->v.z;
		}
	}
}

bool checkMesh(const carve::mesh::MeshSet<3>& meshset) {
	for(auto& mesh : meshset.meshes) {
		for(auto& face : mesh->faces) {
			if(face->edge == nullptr)
				return false;
		}
	}
	return true;
}

void simplifyMesh(meshVector_t& meshVector, bool removeLowVolume) {

	carve::mesh::MeshSimplifier simplifier;

	const double MINIMUM_NORMAL_ANGLE_DEG = 0.1;
	const double MINIMUM_NORMAL_ANGLE = MINIMUM_NORMAL_ANGLE_DEG * M_PI / 180.0;

	int meshSetCount = meshVector.size();
	size_t removedMeshes = 0;
	for(int i=0; i<meshSetCount; ++i) {
		if(!checkMesh(*meshVector[i])) {
			continue;
		}
		try {
//			carve::mesh::MeshSet<3> storedMeshset = *meshVector[i];
			MeshSetInfo info;
			bool checkRes = MeshUtils::checkMeshSetPointers(meshVector[i], false, info);
			if(!checkRes)
				continue;

			removedMeshes += simplifier.mergeCoplanarFaces(meshVector[i].get(), MINIMUM_NORMAL_ANGLE);
			// The following functions can open or destroy the mesh
			//		simplifier.simplify(meshVector[i].get(), 1e-6, 1e-6, MINIMUM_NORMAL_ANGLE, 1e-6);
			//		simplifier.improveMesh_conservative(meshVector[i].get());
			if(removeLowVolume)
				simplifier.removeLowVolumeManifolds( meshVector[i].get(), 1e-8 );
		}
		catch(std::runtime_error& e) {
			MeshUtils::fixMeshset(meshVector[i].get(), 0.001, false, false);
			continue;
		}
	}

}

bool nearEqual(double v1, double v2, double eps) {
	IBK::NearEqual nearEqual(eps);
	return nearEqual(v1,v2);
}

bool nearZero(double val, double eps) {
	return (val + eps >= 0  &&  val <= eps);
}

bool nearEqual(const IBKMK::Vector3D& v1, const IBKMK::Vector3D& v2, double eps) {
	IBK::NearEqual nearEqual(eps);
	if(!nearEqual(v1.m_x,v2.m_x))
		return false;
	if(!nearEqual(v1.m_y,v2.m_y))
		return false;
	if(!nearEqual(v1.m_z,v2.m_z))
		return false;

	return true;
}

double areaPolygon(const std::vector<IBKMK::Vector3D>& poly) {
	if(poly.empty())
		return 0;

	// Newell sum over ALL cyclic edges including the closing edge poly[n-1] x poly[0].
	// Without the closing term the sum is not translation-invariant: a 1 m2 unit
	// square placed at (100,100) yields "51 m2", degenerate needle polygons get
	// large phantom areas, and every area-based matching gate is distorted for
	// geometry far from the origin.
	IBKMK::Vector3D tmp;
	for(size_t i=0; i<poly.size(); ++i) {
		tmp = tmp + poly[i].crossProduct(poly[(i+1)%poly.size()]);
	}
	return tmp.magnitude()*0.5;
}

double areaSignedPolygon(const std::vector<IBKMK::Vector2D>& poly) {
	if(poly.empty())
		return 0;

	double area = 0;
	unsigned int size = poly.size();
	for(unsigned int i=0, j = size-1; i<size; ++i){
		area += (poly[j].m_x + poly[i].m_x) * (poly[j].m_y - poly[i].m_y);
		j=i;
	}

	area *= -0.5;
	return area;
}

double areaSignedPolygon(const std::vector<IBKMK::Vector3D>& poly) {
	if(poly.empty())
		return 0;

	Surface surf(poly);

	return surf.signedArea();
}

std::string guidFromObject(IfcRoot* object) {
	return object->m_GlobalId->m_value;
//	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converterX;
//	return converterX.to_bytes(object->m_GlobalId->m_value);
}

std::string objectTypeToString(BuildingElementTypes type) {
	switch(type) {
		case BET_Beam:					return "beam";
		case BET_Chimney:				return "chimney";
		case BET_Column:					return "column";
		case BET_Covering:				return "covering";
		case BET_CurtainWall:			return "curtain wall";
		case BET_Door:					return "door";
		case BET_Footing:				return "footing";
		case BET_Member:					return "member";
		case BET_Pile:					return "pile";
		case BET_Plate:					return "plate";
		case BET_Railing:				return "railing";
		case BET_Ramp:					return "ramp";
		case BET_RampFlight:				return "ramp flight";
		case BET_Roof:					return "roof";
		case BET_ShadingDevice:			return "shading device";
		case BET_Slab:					return "slab";
		case BET_Stair:					return "stair";
		case BET_StairFlight:			return "stair flight";
		case BET_Wall:					return "wall";
		case BET_Window:					return "window";
		case BET_CivilElement:			return "civil element";
		case BET_DistributionElement:	return "distribution element";
		case BET_ElementAssembly:		return "element assembly";
		case BET_ElementComponent:		return "element component";
		case BET_FeatureElement:			return "feature element";
		case BET_FurnishingElement:		return "furnishing element";
		case BET_GeographicalElement:	return "geographical element";
		case BET_TransportElement:		return "transport element";
		case BET_VirtualElement:			return "virtual element";
		case BET_All:					return "not defined";
		case BET_None:					return "not defined";
	}
	return "not defined";
}

std::string objectTypeToString(ObjectType type) {
	switch(type) {
		case OT_BuildingElement:		return "Building element";
		case OT_Opening:				return "Opening";
		case OT_Space:					return "Space";
		case OT_SpaceBoundary:			return "Space boundary";
		case OT_Site:					return "Site";
		case OT_Building:				return "Building";
		case OT_Storey:					return "Storey";
		case OT_Material:				return "Material";
		case OT_Construction:			return "Construction";
		case OT_Window:					return "Window";
		case OT_Glazing:				return "Glazing";
		case OT_Component:				return "Component";
		case OT_SubComponent:			return "Subsurface component";
		case OT_Instance:				return "Component instance";
		case OT_GeometryConvert:		return "Any object";
		case OT_Unknown:				return "Unkown object";
	}
	return "not defined";
}


BuildingElementTypes getObjectType(const std::shared_ptr<IfcObjectDefinition>& od) {
	if(dynamic_pointer_cast<IfcElement>(od) == nullptr)
		return BET_None;

	if(dynamic_pointer_cast<IfcWall>(od) != nullptr)
		return BET_Wall;
	if(dynamic_pointer_cast<IfcBeam>(od) != nullptr)
		return BET_Beam;
	if(dynamic_pointer_cast<IfcChimney>(od) != nullptr)
		return BET_Chimney;
	if(dynamic_pointer_cast<IfcColumn>(od) != nullptr)
		return BET_Column;
	if(dynamic_pointer_cast<IfcCovering>(od) != nullptr)
		return BET_Covering;
	if(dynamic_pointer_cast<IfcCurtainWall>(od) != nullptr)
		return BET_CurtainWall;
	if(dynamic_pointer_cast<IfcDoor>(od) != nullptr)
		return BET_Door;
	if(dynamic_pointer_cast<IfcFooting>(od) != nullptr)
		return BET_Footing;
	if(dynamic_pointer_cast<IfcMember>(od) != nullptr)
		return BET_Member;
	if(dynamic_pointer_cast<IfcPile>(od) != nullptr)
		return BET_Pile;
	if(dynamic_pointer_cast<IfcPlate>(od) != nullptr)
		return BET_Plate;
	if(dynamic_pointer_cast<IfcRailing>(od) != nullptr)
		return BET_Railing;
	if(dynamic_pointer_cast<IfcRamp>(od) != nullptr)
		return BET_Ramp;
	if(dynamic_pointer_cast<IfcRampFlight>(od) != nullptr)
		return BET_RampFlight;
	if(dynamic_pointer_cast<IfcRoof>(od) != nullptr)
		return BET_Roof;
	if(dynamic_pointer_cast<IfcShadingDevice>(od) != nullptr)
		return BET_ShadingDevice;
	if(dynamic_pointer_cast<IfcSlab>(od) != nullptr)
		return BET_Slab;
	if(dynamic_pointer_cast<IfcStair>(od) != nullptr)
		return BET_Stair;
	if(dynamic_pointer_cast<IfcStairFlight>(od) != nullptr)
		return BET_StairFlight;
	if(dynamic_pointer_cast<IfcWindow>(od) != nullptr)
		return BET_Window;
	if(dynamic_pointer_cast<IfcFeatureElement>(od) != nullptr)
		return BET_FeatureElement;
	if(dynamic_pointer_cast<IfcCivilElement>(od) != nullptr)
		return BET_CivilElement;
	if(dynamic_pointer_cast<IfcDistributionElement>(od) != nullptr)
		return BET_DistributionElement;
	if(dynamic_pointer_cast<IfcElementAssembly>(od) != nullptr)
		return BET_ElementAssembly;
	if(dynamic_pointer_cast<IfcElementComponent>(od) != nullptr)
		return BET_ElementComponent;
	if(dynamic_pointer_cast<IfcFurnishingElement>(od) != nullptr)
		return BET_FurnishingElement;
	if(dynamic_pointer_cast<IfcGeographicElement>(od) != nullptr)
		return BET_GeographicalElement;
	if(dynamic_pointer_cast<IfcTransportElement>(od) != nullptr)
		return BET_TransportElement;
	if(dynamic_pointer_cast<IfcVirtualElement>(od) != nullptr)
		return BET_VirtualElement;

	return BET_All;

}

polyVector_t polylinesFromConnectionGeometry(std::shared_ptr<IfcConnectionGeometry> connectionGeometry,
										  shared_ptr<UnitConverter>& unit_converter,
										  const carve::math::Matrix& spaceTransformation,
										  int objectId,
										  std::vector<ConvertError>& errors) {
	polyVector_t res;

	// connection geometry is set from IFCSpaceBoundary
	if(connectionGeometry == nullptr)
		return res;

	// get geometry data from connection geometry by conversion via ItemShapeData
	shared_ptr<ItemShapeData> item_data(new ItemShapeData);
	std::shared_ptr<IfcConnectionCurveGeometry> curveGeom = std::dynamic_pointer_cast<IfcConnectionCurveGeometry>(connectionGeometry);
	if(curveGeom != nullptr) {
		errors.push_back({OT_SpaceBoundary, objectId, "IfcConnectionCurveGeometry not implemented"});
		return res;
		///< \todo Implement
	}
	std::shared_ptr<IfcConnectionPointEccentricity> pointEccGeom = std::dynamic_pointer_cast<IfcConnectionPointEccentricity>(connectionGeometry);
	if(pointEccGeom != nullptr) {
		errors.push_back({OT_SpaceBoundary, objectId, "IfcConnectionPointEccentricity not implemented"});
		return res;
		///< \todo Implement
	}
	std::shared_ptr<IfcConnectionPointGeometry> pointGeom = std::dynamic_pointer_cast<IfcConnectionPointGeometry>(connectionGeometry);
	if(pointGeom != nullptr) {
		errors.push_back({OT_SpaceBoundary, objectId, "IfcConnectionPointGeometry not implemented"});
		return res;
		///< \todo Implement
	}
	std::shared_ptr<IfcConnectionSurfaceGeometry> surfaceGeom = std::dynamic_pointer_cast<IfcConnectionSurfaceGeometry>(connectionGeometry);
	if(surfaceGeom != nullptr) {
		shared_ptr<IfcSurfaceOrFaceSurface> surface = surfaceGeom->m_SurfaceOnRelatingElement;
		if(surface != nullptr) {
			std::shared_ptr<IfcFaceBasedSurfaceModel> fbSurface = std::dynamic_pointer_cast<IfcFaceBasedSurfaceModel>(surface);
			if(fbSurface != nullptr) {
				errors.push_back({OT_SpaceBoundary, objectId, "IfcFaceBasedSurfaceModel not implemented"});
				return res;
				///< \todo Implement
			}
			std::shared_ptr<IfcFaceSurface> fSurface = std::dynamic_pointer_cast<IfcFaceSurface>(surface);
			if(fSurface != nullptr) {
				errors.push_back({OT_SpaceBoundary, objectId, "IfcFaceSurface not implemented"});
				return res;
				///< \todo Implement
			}
			std::shared_ptr<IfcSurface> nSurface = std::dynamic_pointer_cast<IfcSurface>(surface);
			if(nSurface != nullptr) {
				shared_ptr<GeometrySettings> geom_settings = shared_ptr<GeometrySettings>( new GeometrySettings() );
				RepresentationConverter repConvert(geom_settings, unit_converter);
				bool resConv = repConvert.convertIfcGeometricRepresentationItem(nSurface,item_data, errors);
				if(!resConv)
					return res;

				if(spaceTransformation != carve::math::Matrix::IDENT()) {
					item_data->applyTransformToItem(spaceTransformation, true);
				}
			}
		}
	}
	std::shared_ptr<IfcConnectionVolumeGeometry> volumeGeom = std::dynamic_pointer_cast<IfcConnectionVolumeGeometry>(connectionGeometry);
	if(volumeGeom != nullptr) {
		errors.push_back({OT_SpaceBoundary, objectId, "IfcConnectionVolumeGeometry not implemented"});
		return res;
		///< \todo Implement
	}

	// geometry is converted and transformed into carve::MeshSet<3>
	// now transform it into IBKMK::Vector3D
	meshVector_t meshSetClosedFinal = item_data->m_meshsets;
	meshVector_t meshSetOpenFinal = item_data->m_meshsets_open;
	if(meshSetClosedFinal.empty() && meshSetOpenFinal.empty())
		return res;

	// try to simplify meshes by merging all coplanar faces
	meshVector_t& currentMeshSets =  meshSetClosedFinal.empty() ? meshSetOpenFinal : meshSetClosedFinal;
	if(!currentMeshSets.empty()) {
		simplifyMesh(currentMeshSets, false);
	}

	if(!meshSetClosedFinal.empty()) {
		int msCount = meshSetClosedFinal.size();
		for(int i=0; i<msCount; ++i) {
			res.push_back(std::vector<std::vector<std::vector<IBKMK::Vector3D>>>());
			const carve::mesh::MeshSet<3>& currMeshSet = *meshSetClosedFinal[i];
			convert(currMeshSet, res.back());
		}
	}
	if(!meshSetOpenFinal.empty()) {
		int msCount = meshSetOpenFinal.size();
		for(int i=0; i<msCount; ++i) {
			res.push_back(std::vector<std::vector<std::vector<IBKMK::Vector3D>>>());
			const carve::mesh::MeshSet<3>& currMeshSet = *meshSetOpenFinal[i];
			convert(currMeshSet, res.back());
		}
	}

	return res;
}

bool isIntersected(carve::mesh::MeshSet<3>* a, carve::mesh::MeshSet<3>* b) {
	double CARVE_EPSILON = 1e-5;
	carve::csg::CSG csg(CARVE_EPSILON);

	if(a == nullptr || b == nullptr)
		return false;

	try {
		carve::csg::CSG::meshset_t* res = csg.compute(a, b, carve::csg::CSG::INTERSECTION);
		return !res->meshes.empty();
	}
	catch(...) {
		return false;
	}
}

std::string dumpSurfaces(const std::vector<Surface> &surfaces) {
	std::string res;
	for(size_t i=0; i<surfaces.size(); ++i) {
		res += "Surface: " + std::to_string(i) + "\n";
		const Surface& surf = surfaces[i];
		const auto& poly = surf.polygon();
		for(size_t j=0; j<poly.size(); ++j) {
			res += std::to_string(poly[j].m_x) + " : " + std::to_string(poly[j].m_y) + " : " +std::to_string(poly[j].m_z) + "\n";
		}
		res += "\n";
	}
	return res;
}

inline void safeNormalize(glm::dvec3& vec)
{
	double len = glm::length(vec);
	if( len > EPS_DEFAULT )
	{
		vec = glm::normalize(vec);
	}
}

bool windingOrderPositive(const std::vector<IBKMK::Vector2D>& polygon)
{
	const size_t num_points = polygon.size();
	glm::dvec3 polygon_normal(glm::dvec3(0, 0, 0));
	for( int k = 0; k < num_points; ++k )
	{
		const IBKMK::Vector2D& vertex_current = polygon[k];
		const IBKMK::Vector2D& vertex_next = polygon[(k + 1) % num_points];
		polygon_normal[2] += (vertex_current.m_x - vertex_next.m_x) * (vertex_current.m_y + vertex_next.m_y);
	}
	safeNormalize(polygon_normal);
	return polygon_normal.z > 0;
}

TiXmlElement *writeXMLPolygon2D(const std::vector<IBKMK::Vector2D> &polygon, TiXmlElement *parent) {
	if (polygon.empty())
		return nullptr;

	TiXmlElement * e = new TiXmlElement("Polygon2D");
	parent->LinkEndChild(e);

	std::stringstream vals;
	// 16 significant digits: bit-exact round-trip, matches Surface::writeXMLNew.
	vals << std::setprecision(16);
	for (unsigned int i=0; i<polygon.size(); ++i) {
		vals << polygon[i].m_x << " " << polygon[i].m_y;
		if (i<polygon.size()-1)  vals << ", ";
	}
	TiXmlText * text = new TiXmlText( vals.str() );
	e->LinkEndChild( text );
	return e;
}


} // end namespace
