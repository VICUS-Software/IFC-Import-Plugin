#include "IFCC_Construction.h"

#include <QColor>
#include <QString>

#include <Carve/src/include/carve/carve.hpp>

#include "IFCC_MeshUtils.h"

namespace IFCC {

Construction::Construction() :
	m_basictype(NUM_BT)
{

}

TiXmlElement * Construction::writeXML(TiXmlElement * parent) const {
	TiXmlElement * e = new TiXmlElement("Construction");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));

	if(!m_layers.empty()) {
		TiXmlElement * child = new TiXmlElement("MaterialLayers");
		e->LinkEndChild(child);

		for( const auto& layer : m_layers) {
			TiXmlElement * childLayer = new TiXmlElement("MaterialLayer");
			child->LinkEndChild(childLayer);

			childLayer->SetAttribute("idMaterial", IBK::val2string<unsigned int>(layer.first));
			TiXmlElement::appendIBKParameterElement(childLayer, "Thickness", "m", layer.second);
		}
	}

	return e;
}

VICUS::Construction Construction::getVicusObject(std::map<int,int>& idMap, int idOffset) const {
	VICUS::Construction vcon;
	vcon.m_id = m_id + idOffset;
	idMap[m_id] = vcon.m_id;
	vcon.m_displayName.setString(m_name,"de");

	for(const auto& layer : m_layers) {
		VICUS::MaterialLayer vlay;
		vlay.m_idMaterial = idMap[layer.first];
		vlay.m_para[VICUS::MaterialLayer::P_Thickness].set("Thickness", layer.second, "m");
		vcon.m_materialLayers.push_back(vlay);
	}

	if(!m_color.empty()) {
		QColor c(QString::fromStdString(m_color));
		if(c.isValid())
			vcon.m_color = c;
	}

	return vcon;
}

} // namespace IFCC
