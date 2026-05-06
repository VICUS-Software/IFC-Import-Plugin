#include "mainwindow.h"

#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QDir>
#include <QPluginLoader>

#include "SVImportPluginInterface.h"

/*! Try to load the IFC import plugin directly (bypassing MainWindow's plugin dir
	auto-discovery). Searches next to the binary in typical build layouts:
	./libImportIFCPlugin.so, ../lib_x64/libImportIFCPlugin.so, and
	$(binDir)/../externals/lib_x64/. Returns nullptr on failure. */
static SVImportPluginInterface * loadIFCPlugin(const QString & binDir) {
	QStringList candidates;
	candidates << binDir + "/libImportIFCPlugin.so"
			   << binDir + "/ImportIFCPlugin.dll"
			   << binDir + "/../lib_x64/libImportIFCPlugin.so"
			   << binDir + "/../externals/lib_x64/libImportIFCPlugin.so"
			   << binDir + "/../../externals/lib_x64/libImportIFCPlugin.so";
	for(const QString & c : candidates) {
		if(QFileInfo::exists(c)) {
			QPluginLoader loader(c);
			QObject * obj = loader.instance();
			if(obj) {
				SVImportPluginInterface * plugin = qobject_cast<SVImportPluginInterface *>(obj);
				if(plugin)
					return plugin;
			}
			std::cerr << "[CLI] Failed to load plugin '" << c.toStdString()
					  << "': " << loader.errorString().toStdString() << std::endl;
		}
	}
	return nullptr;
}

/*! Handles the CLI invocation path. Does not return — exits the process. */
[[noreturn]] static void runCLIAndExit(int argc, char *argv[], int parserResult) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("IFC2BESTest");
	QCoreApplication::setApplicationVersion("1.1");

	QCommandLineParser parser;
	parser.setApplicationDescription("IFC → VICUS batch converter (CLI mode).");
	parser.addHelpOption();
	parser.addVersionOption();

	QCommandLineOption inputOpt(QStringList() << "i" << "input",
								 "Input IFC file (.ifc).", "ifcFile");
	QCommandLineOption outputOpt(QStringList() << "o" << "output",
								  "Output VICUS file (.vicus).", "vicusFile");
	QCommandLineOption noSBOpt("no-space-boundaries",
							   "Disable IfcRelSpaceBoundary reuse; synthesize boundaries via construction matching.");
	QCommandLineOption noShadingOpt("no-shading",
									"Disable shading export (construction/similar/opening).");
	QCommandLineOption pluginDirOpt("plugin-dir",
									"Override plugin search directory.", "dir");
	parser.addOption(inputOpt);
	parser.addOption(outputOpt);
	parser.addOption(noSBOpt);
	parser.addOption(noShadingOpt);
	parser.addOption(pluginDirOpt);

	parser.process(app);
	(void)parserResult;

	const QString ifcPath   = parser.value(inputOpt);
	const QString vicusPath = parser.value(outputOpt);
	if(ifcPath.isEmpty() || vicusPath.isEmpty()) {
		std::cerr << "[CLI] --input and --output are required" << std::endl;
		std::cerr << parser.helpText().toStdString() << std::endl;
		std::exit(2);
	}
	if(!QFileInfo::exists(ifcPath)) {
		std::cerr << "[CLI] input file not found: " << ifcPath.toStdString() << std::endl;
		std::exit(3);
	}
	const QString pluginDir = parser.isSet(pluginDirOpt)
			? parser.value(pluginDirOpt)
			: QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();

	SVImportPluginInterface * plugin = loadIFCPlugin(pluginDir);
	if(plugin == nullptr) {
		std::cerr << "[CLI] could not locate ImportIFCPlugin (searched near "
				  << pluginDir.toStdString() << ")" << std::endl;
		std::exit(4);
	}

	// Call the plugin's CLI entry via qobject-cast to the concrete type. Since we
	// only link against the interface here, we resolve runCLI through meta-call.
	bool ok = false;
	QMetaObject::invokeMethod(dynamic_cast<QObject *>(plugin), "runCLI",
							  Qt::DirectConnection,
							  Q_RETURN_ARG(bool, ok),
							  Q_ARG(QString, ifcPath),
							  Q_ARG(QString, vicusPath),
							  Q_ARG(bool, !parser.isSet(noSBOpt)),
							  Q_ARG(bool, !parser.isSet(noShadingOpt)));

	std::cout << "[CLI] " << (ok ? "OK" : "FAILED") << " : "
			  << ifcPath.toStdString() << " -> " << vicusPath.toStdString() << std::endl;
	std::exit(ok ? 0 : 1);
}

int main(int argc, char *argv[])
{
	// Detect CLI mode — any invocation with --input / -i / --help / --version runs
	// headless. All other invocations fall through to the GUI for backwards compat.
	bool cliMode = false;
	for(int i = 1; i < argc; ++i) {
		const std::string a(argv[i]);
		if(a == "-i" || a == "--input" || a.rfind("--input=", 0) == 0 ||
		   a == "-o" || a == "--output" || a.rfind("--output=", 0) == 0 ||
		   a == "-h" || a == "--help" || a == "--version") {
			cliMode = true;
			break;
		}
	}
	if(cliMode)
		runCLIAndExit(argc, argv, 0);

	QApplication a(argc, argv);
	MainWindow w;
	w.show();
	return a.exec();
}
