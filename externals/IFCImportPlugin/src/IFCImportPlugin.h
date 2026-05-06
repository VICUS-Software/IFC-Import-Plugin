#ifndef IFCImportPlugin_H
#define IFCImportPlugin_H

#include "SVImportPluginInterface.h"
#include "ImportIFCMessageHandler.h"

#define IFCImportPlugin_iid "de.dresden-tu.arch.ibk.IFCImportPlugin/1.1"

class IFCImportPlugin : public QObject, public SVImportPluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID IFCImportPlugin_iid FILE "metadata.json")
	Q_INTERFACES(SVImportPluginInterface)

public:
	/*! Default constructor.*/
	explicit IFCImportPlugin(QObject *parent = nullptr);

	/*! Import a VICUS project from an IFC source. It preform the following tasks:
	 *  \li Open a import dialog
	 *  \li Read the ifc and convert it into an internal format
	 *  \li Create a VICUS project text
	 *  The used filename for the IFC file is set and available via IFCFileName() function
	 *  \return Return true if reading and converting was successful
	*/
	virtual bool import(QWidget * parent, QString& projectText) override;

	/*! Return plugin title.*/
	virtual QString title() const override;

	/*! Return string for use as menu caption.*/
	virtual QString importMenuCaption() const override;

	/*! Set the language for the import menu. It will try to install acorresponding translator.
	 *  Each error in this process will be written into the log file (IFCImportPlugin.log).
	*/
	virtual void setLanguage(QString langId, QString appname) override;

	/*! Return the name of the used ifc file or an empty string.*/
	QString IFCFileName() const;

	/*! Headless CLI entry point: read the given IFC file, run the conversion with
		sensible defaults (space boundaries enabled, shading export for construction +
		similar + opening), and write the resulting VICUS project to \a vicusPath.
		Does not show any dialog. Returns true on success. Used by IFC2BESTest CLI
		mode (see main.cpp) for batch testing over a folder of IFC files.
		\param ifcPath Input IFC file.
		\param vicusPath Output VICUS project file (*.vicus).
		\param useSpaceBoundaries When true, reuse IfcRelSpaceBoundary entities; when
			false, space boundaries are synthesized via construction matching.
		\param writeShading When true, enable shading export for construction, similar
			and opening categories.
		Q_INVOKABLE so IFC2BESTest's main.cpp can invoke it via QMetaObject without
		linking against IFCImportPlugin's concrete type.
	*/
	Q_INVOKABLE bool runCLI(const QString & ifcPath, const QString & vicusPath,
							bool useSpaceBoundaries = true, bool writeShading = true);

private:
	ImportIFCMessageHandler m_msgHandler;	///< Message handler for handling warnings, errors and the log file
	QString					m_ifcFileName;	///< Name of the ifc file for converting
};

#endif // IFCImportPlugin_H
