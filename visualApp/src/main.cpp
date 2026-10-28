#include "ofApp.h"
#include "ofMain.h"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace {
bool parseArguments(int argc, char * argv[], bool & diagnosticsEnabled,
	bool & calibrationEnabled) {
	diagnosticsEnabled = false;
	calibrationEnabled = false;

	for (int i = 1; i < argc; ++i) {
		const std::string argument = argv[i];
		if (argument == "--diagnostics") {
			diagnosticsEnabled = true;
			continue;
		}
		if (argument == "--calibration") {
			diagnosticsEnabled = true;
			calibrationEnabled = true;
			continue;
		}

		std::cerr << "visualApp: unknown argument: " << argument << std::endl;
		std::cerr << "Usage: visualApp [--diagnostics|--calibration]" << std::endl;
		return false;
	}

	return true;
}
}

int main(int argc, char * argv[]) {
	bool diagnosticsEnabled = false;
	bool calibrationEnabled = false;
	if (!parseArguments(argc, argv, diagnosticsEnabled, calibrationEnabled)) {
		return EXIT_FAILURE;
	}

	try {
		ofGLWindowSettings settings;
		settings.windowMode = OF_FULLSCREEN;

		auto window = ofCreateWindow(settings);
		if (!window) {
			std::cerr << "visualApp: openFrameworks did not create a window" << std::endl;
			return EXIT_FAILURE;
		}

		ofRunApp(window, std::make_shared<ofApp>(diagnosticsEnabled, calibrationEnabled));
		ofRunMainLoop();
		return EXIT_SUCCESS;
	} catch (const std::exception & error) {
		std::cerr << "visualApp: fatal startup/runtime exception: " << error.what() << std::endl;
	} catch (...) {
		std::cerr << "visualApp: fatal non-standard startup/runtime exception" << std::endl;
	}

	return EXIT_FAILURE;
}
