#include "IFCC_Window.h"

#include <QColor>
#include <QString>

namespace IFCC {

Window::Window() :
	m_id(-1),
	m_glazingSystemId(-1),
	m_methodFrame(3),
	m_methodDivider(3)
{

}

TiXmlElement * Window::writeXML(TiXmlElement * parent) const {
	TiXmlElement * e = new TiXmlElement("Window");
	parent->LinkEndChild(e);

	e->SetAttribute("id", IBK::val2string<unsigned int>(m_id));
	if (!m_name.empty())
		e->SetAttribute("displayName", m_name + "_" + std::to_string(m_id));
	if (m_glazingSystemId > -1)
		e->SetAttribute("idGlazingSystem", IBK::val2string<unsigned int>(m_glazingSystemId));
	if (m_methodFrame < 3)
		e->SetAttribute("methodFrame", IBK::val2string<unsigned int>(m_methodFrame));
	if (m_methodDivider < 3)
		e->SetAttribute("methodDivider", IBK::val2string<unsigned int>(m_methodDivider));
	if (!m_notes.empty())
		TiXmlElement::appendSingleAttributeElement(e, "Notes", nullptr, std::string(), m_notes);
	if (!m_dataSource.empty())
		TiXmlElement::appendSingleAttributeElement(e, "DataSource", nullptr, std::string(), m_dataSource);


	return e;
}

VICUS::Window Window::getVicusObject(std::map<int,int>& idMap, int idOffset) const {
	VICUS::Window vwin;
	vwin.m_id = m_id + idOffset;
	idMap[m_id] = vwin.m_id;
	vwin.m_displayName.setString(m_name,"de");
	vwin.m_idGlazingSystem = idMap[m_glazingSystemId];
	vwin.m_dataSource = QString::fromStdString(m_dataSource);
	vwin.m_notes = QString::fromStdString(m_notes);
	vwin.m_methodFrame = static_cast<VICUS::Window::Method>(m_methodFrame);
	vwin.m_methodDivider = static_cast<VICUS::Window::Method>(m_methodDivider);

	if(!m_color.empty()) {
		QColor c(QString::fromStdString(m_color));
		if(c.isValid())
			vwin.m_color = c;
	}

	return vwin;
}


} // namespace IFCC
