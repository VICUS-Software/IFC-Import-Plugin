#include "IFCC_BuildingElement.h"

#include <IBKMK_Vector3D.h>

#include <carve/mesh.hpp>
#include <carve/matrix.hpp>

#include <ifcpp/IFC4X3/include/IfcRelVoidsElement.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcObjectTypeEnum.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayerSetUsage.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayerSet.h>
#include <ifcpp/IFC4X3/include/IfcMaterialLayer.h>
#include <ifcpp/IFC4X3/include/IfcMaterial.h>
#include <ifcpp/IFC4X3/include/IfcMaterialDefinitionRepresentation.h>
#include <ifcpp/IFC4X3/include/IfcMaterialList.h>
#include <ifcpp/IFC4X3/include/IfcMaterialProfile.h>
#include <ifcpp/IFC4X3/include/IfcMaterialProfileSet.h>

#include <ifcpp/IFC4X3/include/IfcNonNegativeLengthMeasure.h>
#include <ifcpp/IFC4X3/include/IfcPositiveLengthMeasure.h>
#include <ifcpp/IFC4X3/include/IfcWindowStyle.h>
#include <ifcpp/IFC4X3/include/IfcWindowStyleOperationEnum.h>
#include <ifcpp/IFC4X3/include/IfcWindowType.h>
#include <ifcpp/IFC4X3/include/IfcDoorStyle.h>

#include <ifcpp/IFC4X3/include/IfcRelAssignsToProduct.h>
#include <ifcpp/IFC4X3/include/IfcRelFillsElement.h>
#include <ifcpp/IFC4X3/include/IfcRelAssociatesMaterial.h>
#include <ifcpp/IFC4X3/include/IfcRelDefinesByType.h>
#include <ifcpp/IFC4X3/include/IfcRelDefinesByProperties.h>
#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>
#include <ifcpp/IFC4X3/include/IfcElement.h>

#include <ifcpp/IFC4X3/include/IfcPropertySetDefinitionSelect.h>
#include <ifcpp/IFC4X3/include/IfcPropertySetDefinition.h>
#include <ifcpp/IFC4X3/include/IfcPropertySet.h>
#include <ifcpp/IFC4X3/include/IfcProperty.h>
#include <ifcpp/IFC4X3/include/IfcPropertySingleValue.h>
#include <ifcpp/IFC4X3/include/IfcDerivedMeasureValue.h>
#include <ifcpp/IFC4X3/include/IfcMeasureValue.h>
#include <ifcpp/IFC4X3/include/IfcSimpleValue.h>
#include <ifcpp/IFC4X3/include/IfcBinary.h>
#include <ifcpp/IFC4X3/include/IfcBoolean.h>
#include <ifcpp/IFC4X3/include/IfcDate.h>
#include <ifcpp/IFC4X3/include/IfcDateTime.h>
#include <ifcpp/IFC4X3/include/IfcDuration.h>
#include <ifcpp/IFC4X3/include/IfcReal.h>
#include <ifcpp/IFC4X3/include/IfcPositiveInteger.h>
#include <ifcpp/IFC4X3/include/IfcTime.h>
#include <ifcpp/IFC4X3/include/IfcTimeStamp.h>
#include <ifcpp/IFC4X3/include/IfcLogical.h>

#include <ifcpp/IFC4X3/include/IfcMaterialProperties.h>
#include <ifcpp/IFC4X3/include/IfcProduct.h>
#include <ifcpp/IFC4X3/include/IfcSpatialElement.h>
#include <ifcpp/IFC4X3/include/IfcMaterialProperties.h>
#include <ifcpp/IFC4X3/include/IfcRelContainedInSpatialStructure.h>
#include <ifcpp/IFC4X3/include/IfcBuildingElementPart.h>

#include <Carve/src/include/carve/carve.hpp>

#include <sstream>

#include <IBKMK_Polygon3D.h>
#include <IBKMK_Polygon2D.h>

#include <VICUS_Surface.h>
#include <VICUS_SubSurface.h>
#include <VICUS_Polygon2D.h>
#include <VICUS_Hole.h>

#include "IFCC_Clippertools.h"
#include "IFCC_GeometricHelperClasses.h"

#include "IFCC_MeshUtils.h"
#include "IFCC_Helper.h"
#include "IFCC_Logger.h"
#include "IFCC_RepresentationHelper.h"
#include "IFCC_CSG_Adapter.h"

namespace IFCC {

void BuildingElement::WallProperties::update(std::shared_ptr<IFC4X3::IfcWall>& ifcWall) {
	if(!ifcWall)
		return;
	if(ifcWall->m_PredefinedType)
		m_wallType = ifcWall->m_PredefinedType->m_enum;
}

BuildingElement::BuildingElement(int id) :
	EntityBase(id),
	m_constructionId(-1),
	m_type(BET_All),
	m_surfaceComponent(false),
	m_subSurfaceComponent(false)

{

}

bool BuildingElement::set(std::shared_ptr<IFC4X3::IfcElement> ifcElement, BuildingElementTypes type) {
	if(!EntityBase::set(dynamic_pointer_cast<IFC4X3::IfcRoot>(ifcElement)))
		return false;

	m_type = type;
	m_name = label2s(ifcElement->m_Name);
	for(const auto& relop : ifcElement->m_HasOpenings_inverse) {
		if(relop.lock()->m_RelatedOpeningElement)
			m_containedOpeningsOriginal.push_back(relop.lock()->m_RelatedOpeningElement);
	}
	if(isConstructionType(m_type) || isConstructionSimilarType(m_type))
		m_surfaceComponent = true;
	else if(m_type == BET_Window || m_type == BET_Door)
		m_subSurfaceComponent = true;

	// look for properties
	for(const auto& relproperties : ifcElement->m_IsDefinedBy_inverse) {
		shared_ptr<IFC4X3::IfcRelDefinesByProperties> rel_properties(relproperties);
		if(rel_properties && rel_properties->m_RelatingPropertyDefinition) {
			shared_ptr<IFC4X3::IfcPropertySetDefinition> property_set_def = dynamic_pointer_cast<IFC4X3::IfcPropertySetDefinition>(rel_properties->m_RelatingPropertyDefinition);
			if( property_set_def ) {
				shared_ptr<IFC4X3::IfcPropertySet> property_set = dynamic_pointer_cast<IFC4X3::IfcPropertySet>(property_set_def);
				if( property_set ) {
					std::string pset_name = label2s(property_set->m_Name);
					for(const auto& property : property_set->m_HasProperties) {
						std::string name = name2s(property->m_Name);
						bool usesThisProperty = Property::relevantProperty(pset_name,name);
						if(usesThisProperty) {
							Property prop;
							prop.m_name = name;
							getProperty(property,pset_name, prop);
							std::map<std::string, Property> inner;
							inner.insert(std::make_pair(name, prop));
							m_propertyMap.insert(std::make_pair(pset_name, inner));
						}
					}
				}
			}
		}
	}
	setThermalTransmittance();

	if(isOpeningType(m_type)) {
		for(const auto& relop : ifcElement->m_FillsVoids_inverse) {
			shared_ptr<IFC4X3::IfcOpeningElement>& oelem = relop.lock()->m_RelatingOpeningElement;
			if(oelem)
				m_isUsedFromOpeningsOriginal.push_back(oelem);
		}
		if(m_type == BET_Window) {
			shared_ptr<IFC4X3::IfcWindow> window = dynamic_pointer_cast<IFC4X3::IfcWindow>(ifcElement);
			if(window != nullptr) {
				m_openingProperties.m_isWindow = true;
				m_openingProperties.m_isDoor = false;
				if(window->m_PredefinedType != nullptr)
					m_openingProperties.m_windowType = window->m_PredefinedType->m_enum;
				if(window->m_OverallHeight != nullptr)
					m_openingProperties.m_windowHeight = window->m_OverallHeight->m_value;
				if(window->m_OverallWidth != nullptr)
					m_openingProperties.m_windowWidth = window->m_OverallWidth->m_value;
				if(window->m_PartitioningType != nullptr)
					m_openingProperties.m_windowPartitionType = window->m_PartitioningType->m_enum;
				m_openingProperties.m_windowUserDefinedPartitionType = label2s(window->m_UserDefinedPartitioningType);
				switch(m_openingProperties.m_windowType) {
					case IFC4X3::IfcWindowTypeEnum::ENUM_WINDOW: m_openingProperties.m_typeName = "Window"; break;
					case IFC4X3::IfcWindowTypeEnum::ENUM_SKYLIGHT: m_openingProperties.m_typeName = "Skylight"; break;
					case IFC4X3::IfcWindowTypeEnum::ENUM_LIGHTDOME: m_openingProperties.m_typeName = "LightDome"; break;
					case IFC4X3::IfcWindowTypeEnum::ENUM_USERDEFINED: m_openingProperties.m_typeName = "UserDefined"; break;
					case IFC4X3::IfcWindowTypeEnum::ENUM_NOTDEFINED: m_openingProperties.m_typeName = "Not defined"; break;
				}
			}
			for(const auto& reltypes : ifcElement->m_IsTypedBy_inverse) {
				shared_ptr<IFC4X3::IfcRelDefinesByType> rel_types(reltypes);
				shared_ptr<IFC4X3::IfcWindowStyle> windowStyle = dynamic_pointer_cast<IFC4X3::IfcWindowStyle>(rel_types->m_RelatingType);
				if(windowStyle != nullptr) {
					if(windowStyle->m_ConstructionType != nullptr) {

					}
					if(windowStyle->m_OperationType != nullptr) {
						IFC4X3::IfcWindowStyleOperationEnum::IfcWindowStyleOperationEnumEnum type =
								windowStyle->m_OperationType->m_enum;

						switch(type) {
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_SINGLE_PANEL: m_openingProperties.m_windowConstructionTypes.push_back("Single pane"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_DOUBLE_PANEL_VERTICAL: m_openingProperties.m_windowConstructionTypes.push_back("Double pane vertical"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_DOUBLE_PANEL_HORIZONTAL: m_openingProperties.m_windowConstructionTypes.push_back("Double pane horizontal"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_VERTICAL: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane vertical"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_BOTTOM: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane bottom"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_TOP: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane top"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_LEFT: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane left"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_RIGHT: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane right"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_TRIPLE_PANEL_HORIZONTAL: m_openingProperties.m_windowConstructionTypes.push_back("Triple pane horizontal"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_USERDEFINED: m_openingProperties.m_windowConstructionTypes.push_back("User defined panes"); break;
							case IFC4X3::IfcWindowStyleOperationEnum::ENUM_NOTDEFINED: m_openingProperties.m_windowConstructionTypes.push_back("Panes not defined"); break;
						}

					}
				}
				shared_ptr<IFC4X3::IfcWindowType> windowType = dynamic_pointer_cast<IFC4X3::IfcWindowType>(rel_types);
				if(windowType != nullptr) {
//					if(windowType->m_ConstructionType != nullptr) {

//					}
				}
			}
		}
		// door
		else {
			shared_ptr<IFC4X3::IfcDoor> door = dynamic_pointer_cast<IFC4X3::IfcDoor>(ifcElement);
			if(door != nullptr) {
				m_openingProperties.m_isWindow = false;
				m_openingProperties.m_isDoor = true;
				if(door->m_PredefinedType != nullptr)
					m_openingProperties.m_doorType = door->m_PredefinedType->m_enum;
				if(door->m_OverallHeight != nullptr)
					m_openingProperties.m_doorHeight = door->m_OverallHeight->m_value;
				if(door->m_OverallWidth != nullptr)
					m_openingProperties.m_doorWidth = door->m_OverallWidth->m_value;
				if(door->m_OperationType != nullptr)
					m_openingProperties.m_doorOperationType = door->m_OperationType->m_enum;
				for(const auto& reltypes : ifcElement->m_IsTypedBy_inverse) {
					shared_ptr<IFC4X3::IfcRelDefinesByType> rel_types(reltypes);
					shared_ptr<IFC4X3::IfcDoorStyle> doorStyle = dynamic_pointer_cast<IFC4X3::IfcDoorStyle>(rel_types->m_RelatingType);
					if(doorStyle != nullptr) {
						m_openingProperties.m_doorStyleConstructionType = doorStyle->m_ConstructionType->m_enum;
					}
				}
			}
		}
	}

	if(m_type == BET_Wall) {
		shared_ptr<IFC4X3::IfcWall> wall = dynamic_pointer_cast<IFC4X3::IfcWall>(ifcElement);
		m_wallProperties.update(wall);

		if(!wall->m_IsDecomposedBy_inverse.empty()) {
			for(size_t i=0; i<wall->m_IsDecomposedBy_inverse.size(); ++i) {
				shared_ptr<IFC4X3::IfcRelAggregates> relAggregate(wall->m_IsDecomposedBy_inverse[i]);
				if(relAggregate) {
					for(size_t io=0; io<relAggregate->m_RelatedObjects.size(); ++io) {
						const shared_ptr<IFC4X3::IfcObjectDefinition>& object = relAggregate->m_RelatedObjects[io];
						shared_ptr<IFC4X3::IfcElementComponent> comp = dynamic_pointer_cast<IFC4X3::IfcElementComponent>(object);
						shared_ptr<IFC4X3::IfcBuildingElementPart> part = dynamic_pointer_cast<IFC4X3::IfcBuildingElementPart>(comp);
						if(part) {
							m_hasElementParts.push_back(part);
						}

					}
				}
			}
		}
		if(!wall->m_Decomposes_inverse.empty()) {
			for(size_t i=0; i<wall->m_Decomposes_inverse.size(); ++i) {
				shared_ptr<IFC4X3::IfcRelAggregates> relAggregate(wall->m_Decomposes_inverse[i]);
				if(relAggregate) {
					for(size_t io=0; io<relAggregate->m_RelatedObjects.size(); ++io) {
						const shared_ptr<IFC4X3::IfcObjectDefinition>& object = relAggregate->m_RelatedObjects[io];
						shared_ptr<IFC4X3::IfcElementComponent> comp = dynamic_pointer_cast<IFC4X3::IfcElementComponent>(object);
						const shared_ptr<IFC4X3::IfcBuildingElementPart>& part = dynamic_pointer_cast<IFC4X3::IfcBuildingElementPart>(comp);
						if(part) {
							m_hasElementParts.push_back(part);
						}

					}
				}
			}
		}
	}

	for(const auto& relass : ifcElement->m_HasAssociations_inverse) {
		shared_ptr<IFC4X3::IfcRelAssociates> rel_associates(relass);
		shared_ptr<IFC4X3::IfcRelAssociatesMaterial> associated_material = dynamic_pointer_cast<IFC4X3::IfcRelAssociatesMaterial>(rel_associates);
		if (associated_material != nullptr && associated_material->m_RelatingMaterial != nullptr) {
//			std::string classname = associated_material->m_RelatingMaterial->classID();

			shared_ptr<IFC4X3::IfcMaterialDefinitionRepresentation> material_definition_rep = dynamic_pointer_cast<IFC4X3::IfcMaterialDefinitionRepresentation>(associated_material->m_RelatingMaterial);
			if(material_definition_rep) {
				const shared_ptr<IFC4X3::IfcMaterial>& mat = material_definition_rep->m_RepresentedMaterial;
				if (mat) {
					m_materialLayers.emplace_back(std::pair<double,std::string>(0.01, label2s(mat->m_Name)));
					m_materialPropertyMap.emplace_back(std::map<std::string,std::map<std::string,Property>>());
					getMaterialProperties(mat, m_materialPropertyMap.back());
				}
			}

			shared_ptr<IFC4X3::IfcMaterialDefinition> material_definition = dynamic_pointer_cast<IFC4X3::IfcMaterialDefinition>(associated_material->m_RelatingMaterial);
			if(material_definition) {
				shared_ptr<IFC4X3::IfcMaterial> mat = dynamic_pointer_cast<IFC4X3::IfcMaterial>(material_definition);
				if (mat) {
					m_materialLayers.emplace_back(std::pair<double,std::string>(0.01, label2s(mat->m_Name)));
					m_materialPropertyMap.emplace_back(std::map<std::string,std::map<std::string,Property>>());
					getMaterialProperties(mat, m_materialPropertyMap.back());
				}
				shared_ptr<IFC4X3::IfcMaterialLayer> matLayer = dynamic_pointer_cast<IFC4X3::IfcMaterialLayer>(material_definition);
				if (matLayer) {
					const shared_ptr<IFC4X3::IfcMaterial>& mat = matLayer->m_Material;					//optional
					if (mat) {
						m_materialLayers.emplace_back(std::pair<double,std::string>(matLayer->m_LayerThickness->m_value, label2s(mat->m_Name)));
						m_materialPropertyMap.emplace_back(std::map<std::string,std::map<std::string,Property>>());
						getMaterialProperties(mat, m_materialPropertyMap.back());
					}
				}
				shared_ptr<IFC4X3::IfcMaterialLayerSet> matLayerSet = dynamic_pointer_cast<IFC4X3::IfcMaterialLayerSet>(material_definition);
				shared_ptr<IFC4X3::IfcMaterialProfile> matProfile = dynamic_pointer_cast<IFC4X3::IfcMaterialProfile>(material_definition);
				shared_ptr<IFC4X3::IfcMaterialProfileSet> matProfileSet = dynamic_pointer_cast<IFC4X3::IfcMaterialProfileSet>(material_definition);

			}
			shared_ptr<IFC4X3::IfcMaterialList> material_list = dynamic_pointer_cast<IFC4X3::IfcMaterialList>(associated_material->m_RelatingMaterial);
			if(material_list) {
				for(size_t im=0; im<material_list->m_Materials.size(); ++im) {
					const shared_ptr<IFC4X3::IfcMaterial>& mat = material_list->m_Materials[im];
					if (mat) {
						m_materialLayers.emplace_back(std::pair<double,std::string>(0.01, label2s(mat->m_Name)));
						m_materialPropertyMap.emplace_back(std::map<std::string,std::map<std::string,Property>>());
						getMaterialProperties(mat, m_materialPropertyMap.back());
					}
				}
			}
			shared_ptr<IFC4X3::IfcMaterialUsageDefinition> material_usage_definition = dynamic_pointer_cast<IFC4X3::IfcMaterialUsageDefinition>(associated_material->m_RelatingMaterial);
			if(material_usage_definition) {
				shared_ptr<IFC4X3::IfcMaterialLayerSetUsage> material_layer_set_usage = dynamic_pointer_cast<IFC4X3::IfcMaterialLayerSetUsage>(material_usage_definition);
				if (material_layer_set_usage != nullptr && material_layer_set_usage->m_ForLayerSet != nullptr) {
					for (size_t jj = 0; jj < material_layer_set_usage->m_ForLayerSet->m_MaterialLayers.size(); ++jj) {
						const shared_ptr<IFC4X3::IfcMaterialLayer>& material_layer = material_layer_set_usage->m_ForLayerSet->m_MaterialLayers[jj];
						if (material_layer) {
							const shared_ptr<IFC4X3::IfcMaterial>& mat = material_layer->m_Material;					//optional
							if (mat) {
								m_materialLayers.emplace_back(std::pair<double,std::string>(material_layer->m_LayerThickness->m_value, label2s(mat->m_Name)));
								m_materialPropertyMap.emplace_back(std::map<std::string,std::map<std::string,Property>>());
								getMaterialProperties(mat, m_materialPropertyMap.back());
							}
						}
					}
				}
			}

		}
	}

	return true;
}

void BuildingElement::update(std::shared_ptr<ProductShapeData> productShape, std::vector<Opening>& openings, std::vector<ConvertError>& errors, const ConvertOptions& convertOptions) {
	transform(productShape);
	fetchGeometry(productShape, errors, convertOptions.m_distanceEps);
	fetchOpenings(openings, convertOptions.m_distanceEps);
}

void BuildingElement::getShapeOfParts(const std::vector<std::shared_ptr<ProductShapeData>>& partsShapeVect, std::vector<ConvertError>& errors) {
	if(m_hasElementParts.empty())
		return;

	meshVector_t meshSets;

	for(auto part : m_hasElementParts) {
		// find shape for part
		std::shared_ptr<ProductShapeData> shapeData;
		for(auto shape : partsShapeVect) {
			std::shared_ptr<IFC4X3::IfcElement> e = dynamic_pointer_cast<IFC4X3::IfcElement>(shape->m_ifc_object_definition.lock());
			if(e == nullptr)
				continue;

			if(part->m_GlobalId == e->m_GlobalId) {
				shapeData = shape;
				break;
			}
		}

		if(!shapeData)
			continue;

		// Move shape to world coordinates — but only ONCE. IfcBuildingElementPart is both
		// an aggregate child of its parent (processed here) AND a standalone
		// ConstructionSimilar BuildingElement (processed in updateBuildingElements main
		// loop, which also calls applyTransformToProduct). Without this guard the Part
		// gets its full placement chain (site*storey*wall*part) applied twice, which
		// scatters it far from the building envelope.
		if(!shapeData->m_transformAppliedByIFCC) {
			carve::math::Matrix transformMatrix = shapeData->getTransform();
			if(transformMatrix != carve::math::Matrix::IDENT()) {
				shapeData->applyTransformToProduct(transformMatrix, true, false);
			}
			shapeData->m_transformAppliedByIFCC = true;
		}

		// get representation and mesh sets
		std::vector<Surface> surfaces;
		RepresentationStructure repStruct = getRepresentationStructure(shapeData);
		meshVector_t meshSet;
		if(repStruct.m_bodyRep) {
			if(repStruct.m_bodyRepCount > 1) {
				errors.push_back({OT_BuildingElement, m_id, "more than one geometric representaion of type 'body' found"});
			}

			meshVector_t meshSet = finalMeshSet(repStruct.m_bodyRep, errors, surfaces, OT_BuildingElement, m_id);
			meshSets.insert(meshSets.end(), meshSet.begin(), meshSet.end());
		}
		else if(repStruct.m_referenceRep) {
			if(repStruct.m_referenceRepCount > 1) {
				errors.push_back({OT_BuildingElement, m_id, "more than one geometric representaion of type 'reference' found"});
			}
			meshVector_t meshSet = finalMeshSet(repStruct.m_referenceRep, errors, surfaces, OT_BuildingElement, m_id);
			meshSets.insert(meshSets.end(), meshSet.begin(), meshSet.end());
		}
	}

	// return in case we don't have any geometry from building element parts
	if(meshSets.empty())
		return;

	// unify all mesh sets to one
	shared_ptr<carve::mesh::MeshSet<3> > resultMesh;
	shared_ptr<carve::mesh::MeshSet<3> > firstMesh = meshSets.front();
	meshSets.erase(meshSets.begin());
	if(!meshSets.empty()) {
		shared_ptr<GeometrySettings> geom_settings = shared_ptr<GeometrySettings>( new GeometrySettings() );
		CSG_Adapter::computeCSG(firstMesh, meshSets, carve::csg::CSG::UNION, resultMesh, geom_settings);
	}
	else {
		resultMesh = firstMesh;
	}

	// transform unified mesh set to surfaces
	std::vector<Surface> surfaces;
	meshVector_t resultVect;
	if(resultMesh) {
		resultVect.push_back(resultMesh);
		surfacesFromMeshSets(resultVect, surfaces);

		if(!surfaces.empty() && m_surfaces.empty()) {
			m_surfaces = surfaces;
		}
	}

	// initialise surfaces
	for(auto& surf : m_surfaces) {
		surf.set(GUID_maker::instance().guid(), m_id, m_name + "_" + std::to_string(m_id), false);
	}
}


void BuildingElement::transform(std::shared_ptr<ProductShapeData> productShape) {
	if(productShape == nullptr)
		return;

	// Guard against double-apply: for IfcBuildingElementPart the shape is already
	// transformed by the parent element's getShapeOfParts() path.
	if(productShape->m_transformAppliedByIFCC)
		return;

	carve::math::Matrix transformMatrix = productShape->getTransform();
	// Pre-apply bbox for diagnostics: so we can compare translation magnitude vs. result.
	IBKMK::Vector3D preMin(1e30, 1e30, 1e30), preMax(-1e30,-1e30,-1e30);
	bool haveAny = false;
	for(const auto& rep : productShape->m_vec_representations) {
		carve::geom::aabb<3> bb;
		rep->computeBoundingBox(bb);
		if(!bb.isEmpty()) {
			carve::geom::vector<3> lo = bb.min();
			carve::geom::vector<3> hi = bb.max();
			if(lo.x < preMin.m_x) preMin.m_x = lo.x;
			if(lo.y < preMin.m_y) preMin.m_y = lo.y;
			if(lo.z < preMin.m_z) preMin.m_z = lo.z;
			if(hi.x > preMax.m_x) preMax.m_x = hi.x;
			if(hi.y > preMax.m_y) preMax.m_y = hi.y;
			if(hi.z > preMax.m_z) preMax.m_z = hi.z;
			haveAny = true;
		}
	}

	if(transformMatrix != carve::math::Matrix::IDENT()) {
		// Apply to this product's representations only, NOT recursively to m_vec_children.
		// Aggregate children (IfcCurtainWall → IfcPlate, reinforcement groups → bars, etc.)
		// are themselves BuildingElements processed by their own transform() call.
		// getTransform() composes the full placement chain up to this product, so the
		// child's own transform() already puts its mesh into world coords. Recursing here
		// would double-apply the parent's chain to those children's meshes and scatter
		// them far outside the building envelope.
		productShape->applyTransformToProduct(transformMatrix, true, false);
		productShape->m_transformAppliedByIFCC = true;

		if(haveAny) {
			const double tx = transformMatrix.v[12];
			const double ty = transformMatrix.v[13];
			const double tz = transformMatrix.v[14];
			Logger::instance() << "transform '" << m_name << "_" << m_id
							   << "' T=(" << tx << "," << ty << "," << tz
							   << ") preBBox=(" << preMin.m_x << "," << preMin.m_y
							   << "," << preMin.m_z << ")-(" << preMax.m_x << ","
							   << preMax.m_y << "," << preMax.m_z << ")";
		}
	}
}

void BuildingElement::fetchGeometry(std::shared_ptr<ProductShapeData> productShape, std::vector<ConvertError>& errors, double eps) {
	if(productShape == nullptr)
		return;

	std::vector<Surface> partsSurfaces = m_surfaces;

	m_originalMesh = surfacesFromRepresentation(productShape, m_surfaces, errors, OT_BuildingElement, m_id);

	if(m_surfaces.empty() && !partsSurfaces.empty())
		m_surfaces = partsSurfaces;


	// initialise surfaces
	for(auto& surf : m_surfaces) {
		surf.set(GUID_maker::instance().guid(), m_id, m_name + "_" + std::to_string(m_id), false);
	}

	findSurfacePairs(eps);
}

void BuildingElement::findSurfacePairs(double eps) {
	if(m_surfaces.size() < 2)
		return;


	double thickness = 0;
	if(!m_materialLayers.empty()) {
		for(size_t i=0; i<m_materialLayers.size(); ++i) {
			thickness += m_materialLayers[i].first;
		}
	}

	for(int i=0; i<m_surfaces.size()-1; ++i) {
		bool found = false;
		bool foundSide = false;
		for(int j=i+1; j<m_surfaces.size(); ++j) {
			if(m_surfaces[i].isParallelTo(m_surfaces[j], eps)) {
				if(!found) {
					ParallelSurfaces item;
					item.m_indexOrg = i;
					m_parallelSurfaces.push_back(item);
					found = true;
				}
				m_parallelSurfaces.back().m_indicesParallel.push_back(j);
				double dist = m_surfaces[i].distanceToParallelPlane(m_surfaces[j], eps);
				m_parallelSurfaces.back().m_distances.push_back(dist);
				if(thickness > 0 && IBK::nearly_equal<4>(dist,thickness)) {
					if(!foundSide) {
						m_possibleSideSurfaces.push_back(i);
						foundSide = true;
					}
					m_possibleSideSurfaces.push_back(j);
				}
			}
		}
	}

	// Fallback: no side pair was identified by thickness (thickness == 0, or no parallel
	// pair matched the layer thickness). Pick the parallel pair with the largest combined
	// area — for walls/slabs/roofs these are almost always the main front/back faces.
	// Without this, openings cannot classify any of their surfaces as ST_ProbableSide,
	// and subsurface matching falls back to thin reveal/edge faces — windows end up
	// placed on wall end-cap surfaces as narrow strips.
	if(m_possibleSideSurfaces.empty()) {
		double bestArea = -1.0;
		int bestI = -1, bestJ = -1;
		for(const ParallelSurfaces& ps : m_parallelSurfaces) {
			int i = ps.m_indexOrg;
			double areaI = m_surfaces[i].area();
			for(int j : ps.m_indicesParallel) {
				double combined = areaI + m_surfaces[j].area();
				if(combined > bestArea) {
					bestArea = combined;
					bestI = i;
					bestJ = j;
				}
			}
		}
		if(bestI >= 0 && bestJ >= 0) {
			m_possibleSideSurfaces.push_back(bestI);
			m_possibleSideSurfaces.push_back(bestJ);
			Logger::instance() << "findSurfacePairs: thickness-based side pair not found for element id="
							   << m_id << " name='" << m_name << "' thickness=" << thickness
							   << " — using largest-area parallel pair (indices " << bestI
							   << "," << bestJ << " combined area=" << bestArea << ")";
		}
		else {
			Logger::instance() << "findSurfacePairs: no parallel pair at all for element id="
							   << m_id << " name='" << m_name << "' surfaces=" << m_surfaces.size();
		}
	}
}

void BuildingElement::fetchOpenings(std::vector<Opening>& openings, double eps) {

	for(const auto& opOrg : m_isUsedFromOpeningsOriginal) {
		if(!opOrg)
			continue;

		for(auto& op : openings) {
			std::string guid = guidFromObject(opOrg.get());
			if(op.guid() == guid) {
				m_usedFromOpenings.push_back(op.m_id);
				op.addOpeningElementId(m_id);
				break;
			}
		}
	}

	for(const auto& opOrg : m_containedOpeningsOriginal) {
		if(!opOrg)
			continue;
		for(auto& op : openings) {
			std::string guid = guidFromObject(opOrg.get());
			if(op.guid() == guid) {
				m_containedOpenings.push_back(op.m_id);
				op.addContainingElementId(m_id);
				break;
			}
		}
	}

	// check openings
	for(const auto& op : m_containedOpenings) {
		auto fit = std::find_if(
					   openings.begin(),
					   openings.end(),
					   [op](const auto& opening) {return opening.m_id == op; });
		if(fit == openings.end())
			continue;

		fit->createCSGSurfaces(*this, eps);
		fit->checkSurfaceType(*this, eps);
	}
}

const std::vector<Surface>& BuildingElement::surfaces() const {
	return m_surfaces;
}

double	BuildingElement::thickness() const {
	if(m_materialLayers.empty()) {
		if(m_parallelSurfaces.empty())
			return 0;

		double minDist = 10001;
		for(const auto& item : m_parallelSurfaces) {
			minDist = std::min(minDist, item.minDistance());
		}
		if(minDist > 10000)
			return 0;

		return minDist;
	}

	double res = 0;
	for(size_t i=0; i<m_materialLayers.size(); ++i) {
		res += m_materialLayers[i].first;
	}
	return res;
}

double BuildingElement::openingArea() const {
	if(!isSubSurfaceComponent())
		return 0;

	if(m_openingProperties.m_isWindow) {
		return m_openingProperties.m_windowHeight * m_openingProperties.m_windowWidth;
	}
	if(m_openingProperties.m_isDoor) {
		return m_openingProperties.m_doorHeight * m_openingProperties.m_doorWidth;
	}

	return 0;
}

TiXmlElement *BuildingElement::writeXML(TiXmlElement *parent, const ConvertOptions& convertOptions) const {
	if (m_id == -1)
		return nullptr;

	bool hasSurface = false;
	for(auto surf : m_surfaces) {
		if(surf.check(convertOptions.m_polygonEps))
			hasSurface = true;
	}
	if(!hasSurface)
		return nullptr;

	TiXmlElement * e = new TiXmlElement("PlainGeometry");
	parent->LinkEndChild(e);

	if(!m_surfaces.empty()) {
		TiXmlElement * child = new TiXmlElement("Surfaces");
		e->LinkEndChild(child);
		for(auto surf : m_surfaces) {
			surf.writeXML(child, convertOptions);
		}
	}

}

void BuildingElement::setContainingElements(const std::vector<Opening>& openings) {
	if(!isSubSurfaceComponent())
		return;

	m_openingProperties.m_usedInConstructionIds.clear();
	for(const auto& opId : m_usedFromOpenings) {
		auto fit = std::find_if(openings.begin(), openings.end(),
								[opId](const auto& op) -> bool {return op.m_id == opId; });
		if(fit != openings.end()) {
			fit->insertContainingElementId(m_openingProperties.m_usedInConstructionIds);
		}
	}
}

void BuildingElement::setContainedConstructionThickesses(const std::vector<std::shared_ptr<BuildingElement>>& elements) {
	if(!isSubSurfaceComponent())
		return;

	int constructionIDCount = m_openingProperties.m_usedInConstructionIds.size();
	for(int i=0; i<constructionIDCount; ++i) {
		int constId = m_openingProperties.m_usedInConstructionIds[i];
		auto fit = std::find_if(elements.begin(), elements.end(),
								[constId](const auto& constr) -> bool {return constr->m_id == constId; });
		if(fit != elements.end()) {
			m_openingProperties.m_constructionThicknesses.push_back((*fit)->thickness());
		}
	}
}

void BuildingElement::setThermalTransmittance() {
	double tt = 0;
	if(m_type == BET_Wall && getDoubleProperty(m_propertyMap, "Pset_WallCommon", "ThermalTransmittance", tt))
		m_thermalTransmittance = tt;
	if(m_type == BET_Window && getDoubleProperty(m_propertyMap, "Pset_WindowCommon", "ThermalTransmittance", tt))
		m_thermalTransmittance = tt;
	if(m_type == BET_Door && getDoubleProperty(m_propertyMap, "Pset_DoorCommon", "ThermalTransmittance", tt))
		m_thermalTransmittance = tt;
}

namespace {

/*! Convert an IFCC::Surface to a VICUS::Surface for use in a shading object.
	Mirrors the validation/round-trip logic used in SpaceBoundary::getVicusSurface so
	surfaces that would fail XML round-trip are rejected here and skipped by the caller.
	Surfaces with an area below m_minimumSurfaceArea are rejected silently to avoid
	polluting the output with sliver faces produced by Carve CSG subtractions.
	Returns a surface with m_id == INVALID_ID on failure. */
VICUS::Surface ifccSurfaceToVicusSurface(const Surface& s, const ConvertOptions& options, const std::string& guid) {
	VICUS::Surface vsurf;

	if(!s.check(options.m_polygonEps))
		return vsurf;

	if(s.area() < options.m_minimumSurfaceArea)
		return vsurf;

	const std::vector<IBKMK::Vector3D>& polyVect = s.polygon();
	if(polyVect.size() < 3) {
		Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
			<< ") has less than 3 vertices - skipping";
		return vsurf;
	}

	IBKMK::Polygon3D poly3D(polyVect);
	if(!poly3D.isValid()) {
		Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
			<< ") has invalid 3D polygon - skipping";
		return vsurf;
	}

	// Simulate VICUS XML round-trip to catch precision-related losses early.
	{
		IBKMK::Vector3D rtOffset, rtNormal, rtLocalX;
		try {
			rtOffset = IBKMK::Vector3D::fromString(poly3D.offset().toString());
			rtNormal = IBKMK::Vector3D::fromString(poly3D.normal().toString());
			rtLocalX = IBKMK::Vector3D::fromString(poly3D.localX().toString());
		}
		catch (...) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") offset/normal/localX vectors fail serialization round-trip - skipping";
			return vsurf;
		}

		const std::vector<IBKMK::Vector2D>& polylineVerts = poly3D.polyline().vertexes();
		std::vector<IBKMK::Vector2D> rtVerts(polylineVerts.size());
		for(size_t i = 0; i < polylineVerts.size(); ++i) {
			std::stringstream ss;
			ss << polylineVerts[i].m_x << " " << polylineVerts[i].m_y;
			ss >> rtVerts[i].m_x >> rtVerts[i].m_y;
		}

		IBKMK::Polygon2D rtPoly2D(rtVerts);
		if(!rtPoly2D.isValid()) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") polygon will not survive XML round-trip (invalid polyline) - skipping";
			return vsurf;
		}

		IBKMK::Polygon3D rtPoly3D(rtPoly2D, rtOffset, rtNormal, rtLocalX);
		if(!rtPoly3D.isValid()) {
			Logger::instance() << "Warning: Surface '" << s.name() << "' (id " << s.id()
				<< ") polygon will not survive XML round-trip (invalid polygon3D) - skipping";
			return vsurf;
		}
	}

	vsurf.m_id = s.id();
	vsurf.m_displayName = QString::fromStdString(s.name());
	vsurf.m_ifcGUID = guid;
	vsurf.setPolygon3D(poly3D);

	std::vector<VICUS::SubSurface> vicusSubSurfaces;
	for(const auto& sub : s.subSurfaces()) {
		if(!sub.isValid())
			continue;
		if(sub.isHole())
			continue;

		const std::vector<IBKMK::Vector2D>& poly2D = sub.polygon();
		if(poly2D.size() < 3) {
			Logger::instance() << "Warning: SubSurface '" << sub.name() << "' (id " << sub.id()
				<< ") has less than 3 vertices - skipping";
			continue;
		}

		VICUS::Polygon2D vicusPoly2D(poly2D);
		if(!vicusPoly2D.isValid()) {
			Logger::instance() << "Warning: SubSurface '" << sub.name() << "' (id " << sub.id()
				<< ") has invalid 2D polygon - skipping";
			continue;
		}

		VICUS::SubSurface vsub;
		vsub.m_id = sub.id();
		vsub.m_displayName = QString::fromStdString(sub.name());
		vsub.m_polygon2D = vicusPoly2D;
		vicusSubSurfaces.push_back(vsub);
	}
	if(!vicusSubSurfaces.empty())
		vsurf.setSubSurfaces(vicusSubSurfaces);

	return vsurf;
}

} // anonymous namespace

namespace {

/*! Group of coplanar polygons sharing one reference plane. */
struct CoplanarGroup {
	IBKMK::Vector3D		m_normalCanon;	///< Canonicalized (consistent-sign) unit normal
	double				m_distanceCanon;///< Hesse distance matching the canonicalized normal
	PlaneNormal			m_plane;		///< Reference plane (built from the first polygon of the group)
	std::vector<polygon3D_t> m_polygons;
};

/*! Canonicalize a unit normal and Hesse distance so that coplanar faces with
	opposite normal orientation end up in the same group. Flips the sign so the
	first component above the epsilon is positive. */
static void canonicalizePlane(IBKMK::Vector3D& n, double& d) {
	const double eps = 1e-8;
	bool flip = false;
	if(std::abs(n.m_x) > eps)
		flip = n.m_x < 0;
	else if(std::abs(n.m_y) > eps)
		flip = n.m_y < 0;
	else
		flip = n.m_z < 0;
	if(flip) {
		n = IBKMK::Vector3D(-n.m_x, -n.m_y, -n.m_z);
		d = -d;
	}
}

/*! Bucket faces of a building element into coplanar groups and union each group
	via clipper. Disjoint coplanar faces are kept separate (clipper returns them
	as separate result rings). Inner contours (holes) — e.g. where an IfcOpeningElement
	was CSG-subtracted from a wall face — are preserved and returned with their
	containing outer ring. Falls back to the input polygons (without holes) if the
	union fails, so geometry is never silently lost. */
static std::vector<CoplanarUnionRing> mergeCoplanarFaces(const std::vector<Surface>& surfaces, double polygonEps) {
	const double normalTol = 1e-3;	///< dot(n1,n2) > 1 - normalTol → considered parallel
	const double distTol   = 1e-3;	///< m — plane offset match tolerance

	std::vector<CoplanarGroup> groups;
	for(const Surface& s : surfaces) {
		const std::vector<IBKMK::Vector3D>& poly = s.polygon();
		if(poly.size() < 3)
			continue;

		IBKMK::Vector3D n = s.cachedHesse().m_n0;
		double d = s.cachedHesse().m_d;
		if(n.magnitude() < 1e-8)
			continue;
		n.normalize();
		canonicalizePlane(n, d);

		CoplanarGroup* match = nullptr;
		for(CoplanarGroup& g : groups) {
			double dot = n.m_x * g.m_normalCanon.m_x
					   + n.m_y * g.m_normalCanon.m_y
					   + n.m_z * g.m_normalCanon.m_z;
			if(dot > 1.0 - normalTol && std::abs(d - g.m_distanceCanon) < distTol) {
				match = &g;
				break;
			}
		}
		if(match == nullptr) {
			groups.emplace_back();
			match = &groups.back();
			match->m_normalCanon = n;
			match->m_distanceCanon = d;
			match->m_plane = PlaneNormal(poly);
			if(!match->m_plane.m_valid) {
				// PlaneNormal(polygon) relies on polygon[0], [1] and back — for self-touching
				// polygons with a seam these can be collinear, producing a degenerate basis.
				// Try rotating the vertex order to find a non-degenerate starting triple —
				// safer than PlaneNormal(hesse, polygon) which has an lx/ly swap bug for
				// axis-aligned planes (YZPlane in particular).
				polygon3D_t rotated = poly;
				bool recovered = false;
				for(size_t shift = 1; shift < poly.size(); ++shift) {
					std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
					PlaneNormal candidate(rotated);
					if(candidate.m_valid) {
						match->m_plane = candidate;
						recovered = true;
						break;
					}
				}
				if(!recovered) {
					Logger::instance() << "shadingExport merge: dropping group — PlaneNormal"
									   << " unrecoverable via vertex rotation ("
									   << poly.size() << " verts)";
					groups.pop_back();
					continue;
				}
			}
		}
		match->m_polygons.push_back(poly);
	}

	std::vector<CoplanarUnionRing> result;
	size_t fallbackGroups = 0;
	for(CoplanarGroup& g : groups) {
		// Always run through clipper, even for single-polygon groups: Carve frequently
		// emits non-simple boundary polygons for CSG-subtracted wall faces (the boundary
		// winds through a zero-width seam to enclose a window cutout). Clipper with
		// pftNonZero decomposes these into proper outer+hole pairs via PolyTree.
		std::vector<CoplanarUnionRing> merged = unionCoplanarPolygons(g.m_polygons, g.m_plane);
		if(merged.empty()) {
			++fallbackGroups;
			// Fallback: emit originals unmerged and without holes so geometry is not lost.
			for(const polygon3D_t& p : g.m_polygons) {
				CoplanarUnionRing ring;
				ring.m_outer = p;
				cleanPolygon(ring.m_outer, polygonEps);
				if(ring.m_outer.size() >= 3)
					result.push_back(ring);
			}
		}
		else {
			// Do NOT call cleanPolygon here — it constructs its own PlaneNormal from the
			// first three vertices of the merged ring, which can be collinear for clipper
			// output and trashes the polygon via a degenerate 2D↔3D round-trip.
			// IBKMK::Polygon3D::setVertexes() handles collinear-point elimination itself.
			for(CoplanarUnionRing& r : merged) {
				if(r.m_outer.size() >= 3)
					result.push_back(r);
			}
		}
	}
	if(fallbackGroups > 0)
		Logger::instance() << "shadingExport merge: " << fallbackGroups
						   << "/" << groups.size() << " coplanar groups fell back to"
						   << " raw input (clipper returned empty)";
	return result;
}

/*! Convert a merged outer ring (with optional holes) into a VICUS::Surface using
	the explicit-basis IBKMK::Polygon3D constructor.
	Uses the plane basis captured in CoplanarUnionRing so IBKMK::Polygon3D doesn't
	have to re-infer the plane from the vertices — that inference was rejecting
	large wall front faces with many vertices via false-positive isSimplePolygon
	checks caused by precision drift from re-projected 3D points. Hole 2D coords
	are carried through in the same basis, so they land correctly on the parent
	surface's local frame after the constant shift (first outer vertex → (0,0)).
*/
VICUS::Surface mergedRingToVicusSurface(const CoplanarUnionRing& ring,
										 const ConvertOptions& options,
										 int surfaceId,
										 const std::string& name,
										 const std::string& guid) {
	VICUS::Surface vsurf;

	if(ring.m_outer2D.size() < 3 || ring.m_outer.size() < 3) {
		Logger::instance() << "shadingExport skip: outer ring '" << name
						   << "' (id " << surfaceId << ") has "
						   << ring.m_outer2D.size() << " verts (2D)";
		return vsurf;
	}

	// Check area against minimum threshold — reuse IFCC::Surface::area() for consistency.
	{
		Surface tmp(ring.m_outer);
		const double a = tmp.area();
		if(a < options.m_minimumSurfaceArea) {
			Logger::instance() << "shadingExport skip: outer ring '" << name
							   << "' (id " << surfaceId << ") area=" << a
							   << " below minimum " << options.m_minimumSurfaceArea;
			return vsurf;
		}
	}

	// Gram-Schmidt orthonormalize the plane basis. IFCC::PlaneNormal builds m_lx
	// from a raw polygon edge; if Carve's mesh is not perfectly planar (common
	// for CSG output on thin concrete slabs / beams) m_lx can end up not exactly
	// perpendicular to m_lz, and IBKMK::Polygon3D::setRotation throws because it
	// tolerates only ~1e-12 drift on the dot product.
	IBKMK::Vector3D normal = ring.m_planeNormal;
	const double nMag = normal.magnitude();
	if(nMag < 1e-12) {
		Logger::instance() << "shadingExport skip: outer ring '" << name
						   << "' (id " << surfaceId << ") degenerate plane normal";
		return vsurf;
	}
	normal *= (1.0 / nMag);
	IBKMK::Vector3D localX = ring.m_planeLocalX
			- normal * ring.m_planeLocalX.scalarProduct(normal);
	const double lxMag = localX.magnitude();
	if(lxMag < 1e-9) {
		Logger::instance() << "shadingExport skip: outer ring '" << name
						   << "' (id " << surfaceId << ") localX collapsed to zero after orthogonalization";
		return vsurf;
	}
	localX *= (1.0 / lxMag);
	IBKMK::Vector3D localY;
	normal.crossProduct(localX, localY);
	localY.normalize();

	// Re-project the outer ring and holes into this clean orthonormal basis. We
	// work from the original 3D points so any tiny in-plane drift introduced by
	// the old 2D↔3D pipe is compensated away here. All resulting 2D coords live
	// in (localX, localY) with origin at ring.m_planeOffset.
	std::vector<IBKMK::Vector2D> outerReproj;
	outerReproj.reserve(ring.m_outer.size());
	for(const IBKMK::Vector3D& p : ring.m_outer) {
		const IBKMK::Vector3D rel = p - ring.m_planeOffset;
		outerReproj.emplace_back(rel.scalarProduct(localX), rel.scalarProduct(localY));
	}

	// Shift the 2D polyline so the first outer vertex becomes (0,0) — required by
	// IBKMK::Polygon3D's explicit-basis constructor. Carry the shift into 3D.
	const IBKMK::Vector2D shift = outerReproj.front();
	const IBKMK::Vector3D offset3D = ring.m_planeOffset
			+ localX * shift.m_x
			+ localY * shift.m_y;

	std::vector<IBKMK::Vector2D> outerShifted;
	outerShifted.reserve(outerReproj.size());
	for(const IBKMK::Vector2D& v : outerReproj)
		outerShifted.emplace_back(v.m_x - shift.m_x, v.m_y - shift.m_y);

	IBKMK::Polygon2D outer2D(outerShifted);
	if(!outer2D.isValid()) {
		std::stringstream vertsStr;
		for(size_t i = 0; i < outerShifted.size(); ++i) {
			if(i > 0) vertsStr << " | ";
			vertsStr << "(" << outerShifted[i].m_x << "," << outerShifted[i].m_y << ")";
		}
		Logger::instance() << "shadingExport skip: outer ring '" << name
						   << "' (id " << surfaceId << ") 2D polyline invalid ("
						   << outerShifted.size() << " verts, " << ring.m_holes.size()
						   << " holes) all=[" << vertsStr.str() << "]";
		return vsurf;
	}

	IBKMK::Polygon3D poly3D(outer2D, offset3D, normal, localX);
	if(!poly3D.isValid()) {
		Logger::instance() << "shadingExport skip: outer ring '" << name
						   << "' (id " << surfaceId
						   << ") explicit-basis Polygon3D invalid — offset=("
						   << offset3D.m_x << "," << offset3D.m_y << "," << offset3D.m_z
						   << ") n=(" << normal.m_x << "," << normal.m_y << "," << normal.m_z
						   << ") lx=(" << localX.m_x << "," << localX.m_y << "," << localX.m_z
						   << ") n·lx=" << normal.scalarProduct(localX);
		return vsurf;
	}

	vsurf.m_id = surfaceId;
	// Each shading surface gets a unique displayName including its own surface id so
	// individual surfaces can be identified in the VICUS UI (before, all surfaces of a
	// ShadingObject shared the parent element's displayName and were indistinguishable
	// except by their numeric id attribute).
	vsurf.m_displayName = QString::fromStdString(name + "_s" + std::to_string(surfaceId));
	vsurf.m_ifcGUID = guid;
	vsurf.setPolygon3D(poly3D);

	// Re-project hole 3D vertices into the same orthonormal basis (and the same
	// shifted origin) used for the outer ring. Using the raw clipper 2D coords
	// here would drift against the orthogonalized outer frame.
	std::vector<VICUS::Hole> vicusHoles;
	size_t holesDropped = 0;
	for(const polygon3D_t& hole3D : ring.m_holes) {
		if(hole3D.size() < 3) {
			++holesDropped;
			continue;
		}
		std::vector<IBKMK::Vector2D> hole2D;
		hole2D.reserve(hole3D.size());
		for(const IBKMK::Vector3D& p : hole3D) {
			const IBKMK::Vector3D rel = p - ring.m_planeOffset;
			const double x = rel.scalarProduct(localX);
			const double y = rel.scalarProduct(localY);
			hole2D.emplace_back(x - shift.m_x, y - shift.m_y);
		}

		VICUS::Polygon2D vicusPoly2D(hole2D);
		if(!vicusPoly2D.isValid()) {
			++holesDropped;
			continue;
		}
		VICUS::Hole vh;
		vh.m_id = GUID_maker::instance().guid();
		vh.m_holePolygon = vicusPoly2D;
		vicusHoles.push_back(vh);
	}
	if(holesDropped > 0)
		Logger::instance() << "shadingExport surface '" << name
						   << "' (id " << surfaceId << "): "
						   << holesDropped << "/" << ring.m_holes.size()
						   << " holes dropped (invalid 2D polygon)";
	if(!vicusHoles.empty())
		vsurf.setHoles(vicusHoles);

	return vsurf;
}

} // anonymous namespace

VICUS::ShadingObject BuildingElement::getVicusShadingObject(const ConvertOptions& options) const {
	VICUS::ShadingObject shading;

	std::vector<VICUS::Surface> vicusSurfaces;

	if(!options.m_mergeShadingCoplanarFaces) {
		for(const Surface& s : m_surfaces) {
			VICUS::Surface vsurf = ifccSurfaceToVicusSurface(s, options, m_guid);
			if(vsurf.m_id != INVALID_ID)
				vicusSurfaces.push_back(vsurf);
		}
	}
	else {
		// Log AABB of the raw input faces — helps spot elements whose mesh is placed
		// at unexpected world coords (e.g. double-transformed shapes) vs. the usual
		// building envelope derived from SpaceBoundaries.
		if(!m_surfaces.empty()) {
			IBKMK::Vector3D bbMin(1e30, 1e30, 1e30);
			IBKMK::Vector3D bbMax(-1e30, -1e30, -1e30);
			for(const Surface& s : m_surfaces) {
				for(const IBKMK::Vector3D& p : s.polygon()) {
					if(p.m_x < bbMin.m_x) bbMin.m_x = p.m_x;
					if(p.m_y < bbMin.m_y) bbMin.m_y = p.m_y;
					if(p.m_z < bbMin.m_z) bbMin.m_z = p.m_z;
					if(p.m_x > bbMax.m_x) bbMax.m_x = p.m_x;
					if(p.m_y > bbMax.m_y) bbMax.m_y = p.m_y;
					if(p.m_z > bbMax.m_z) bbMax.m_z = p.m_z;
				}
			}
			Logger::instance() << "shadingExport bbox '" << m_name << "_" << m_id
							   << "' min=(" << bbMin.m_x << "," << bbMin.m_y << "," << bbMin.m_z
							   << ") max=(" << bbMax.m_x << "," << bbMax.m_y << "," << bbMax.m_z
							   << ") faces=" << m_surfaces.size();
		}

		std::vector<CoplanarUnionRing> merged = mergeCoplanarFaces(m_surfaces, options.m_polygonEps);
		const std::string baseName = m_name + "_" + std::to_string(m_id);
		for(const CoplanarUnionRing& ring : merged) {
			VICUS::Surface vsurf = mergedRingToVicusSurface(
				ring, options, GUID_maker::instance().guid(), baseName, m_guid);
			if(vsurf.m_id != INVALID_ID)
				vicusSurfaces.push_back(vsurf);
		}
		// Per-element summary — spots cases where a wall's front/back is silently gone.
		// In=raw Carve faces, merged=rings after coplanar union, out=surfaces that made it.
		if(merged.size() != vicusSurfaces.size()) {
			Logger::instance() << "shadingExport element '"
							   << m_name << "_" << m_id << "' in=" << m_surfaces.size()
							   << " merged=" << merged.size()
							   << " out=" << vicusSurfaces.size()
							   << " dropped=" << (merged.size() - vicusSurfaces.size());
		}
	}

	if(vicusSurfaces.empty())
		return shading;

	shading.m_id = m_id;
	// Display name format: "<name>_<element_id> [ifc:<step_id> guid:<globalid>]"
	// The element id helps distinguish elements with the same name; the ifcTag (step
	// file line number) and IFC GlobalId let you jump from a VICUS scene object back
	// to its exact IFC source entity.
	{
		std::ostringstream oss;
		oss << m_name << "_" << m_id;
		if(m_ifcId > 0 || !m_guid.empty()) {
			oss << " [";
			if(m_ifcId > 0)
				oss << "ifc:#" << m_ifcId;
			if(!m_guid.empty()) {
				if(m_ifcId > 0) oss << " ";
				oss << "guid:" << m_guid;
			}
			oss << "]";
		}
		shading.m_displayName = QString::fromStdString(oss.str());
	}
	shading.m_surfaces = vicusSurfaces;
	return shading;
}


} // namespace IFCC
