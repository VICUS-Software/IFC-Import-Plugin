#include "IFCC_SubSurfaceComponent.h"

#include <QColor>
#include <QString>

namespace IFCC {

SubSurfaceComponent::SubSurfaceComponent() :
	m_id(-1),
	m_type(NUM_CT),
	m_windowId(-1),
	m_constructionId(-1)
{
}

SubSurfaceComponent::SubSurfaceComponent(int id, const std::string guid, const std::string& name) :
	m_type(NUM_CT),
	m_windowId(-1),
	m_constructionId(-1),
	m_id(id),
	m_name(name),
	m_guid(guid)
{
}

void SubSurfaceComponent::setWindow(int id) {
	m_type = CT_Window;
	m_windowId = id;
}

void SubSurfaceComponent::setDoor(int id) {
	m_type = CT_Door;
	m_constructionId = id;
}

void SubSurfaceComponent::setOther(int id) {
	m_type = CT_Miscellaneous;
	m_constructionId = id;
}

VICUS::SubSurfaceComponent SubSurfaceComponent::getVicusObject(std::map<int,int>& idMap, int idOffset) const {
	VICUS::SubSurfaceComponent vssc;
	vssc.m_id = m_id + idOffset;
	idMap[m_id] = vssc.m_id;
	vssc.m_displayName.setString(m_name,"de");
	vssc.m_type = static_cast<VICUS::SubSurfaceComponent::SubSurfaceComponentType>(m_type);
	if(m_windowId != -1) {
		auto fit = idMap.find(m_windowId);
		if(fit != idMap.end())
			vssc.m_idWindow = fit->second;
	}
	if(m_constructionId != -1) {
		auto fit = idMap.find(m_constructionId);
		if(fit != idMap.end())
			vssc.m_idConstruction = fit->second;
	}
	if(!m_color.empty()) {
		QColor c(QString::fromStdString(m_color));
		if(c.isValid())
			vssc.m_color = c;
	}
	return vssc;
}

TiXmlElement * SubSurfaceComponent::writeXML(TiXmlElement * parent) const {
	TiXmlElement * e = new TiXmlElement("SubSurfaceComponent");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));

	if (m_type != NUM_CT)
		TiXmlElement::appendSingleAttributeElement(e, "Type", nullptr, std::string(), type2String(m_type));
	if (m_windowId != -1)
		TiXmlElement::appendSingleAttributeElement(e, "IdWindow", nullptr, std::string(), IBK::val2string<unsigned int>(m_windowId));
	if (m_constructionId != -1)
		TiXmlElement::appendSingleAttributeElement(e, "IdConstruction", nullptr, std::string(), IBK::val2string<unsigned int>(m_constructionId));


	return e;
}

std::string SubSurfaceComponent::type2String(SubSurfaceComponentType type) const {
	switch(type) {
		case CT_Window: return "Window";
		case CT_Door: return "Door";
		case CT_Miscellaneous: return "Miscellaneous";
		case NUM_CT: return "Unknown";
	}
}

} // namespace IFCC
