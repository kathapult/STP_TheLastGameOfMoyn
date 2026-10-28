#include "ofApp.h"
#include "ofMain.h"
#include "ofAppNoWindow.h"
#include <cstdlib>
#include <iostream>
#include <string>

// Window size for the debug view: two 512x424 depth-resolution panels side by
// side, plus DebugView's margins, caption strip and the three-line
// settings/key footer underneath.
static const int DEBUG_WINDOW_WIDTH = 1054;
static const int DEBUG_WINDOW_HEIGHT = 530;

// Pi A normally never renders (FR-003) - windowless via ofAppNoWindow, no GL
// context, no per-frame buffer swap. Three maintenance flags change that:
//
//   --debug    open a real window showing the detection stages (DebugView),
//              the on-device-debugging fallback research.md item 5 anticipated.
//              Needs a monitor on Pi A.
//   --preview  stream a small copy of the mask to Pi B instead, which is how
//              the same picture is watched when Pi A has no monitor - the
//              usual case. Independent of --debug; both may be passed.
//   --calibration  enable preview plus Pi B's source-filtered settings
//                  control endpoint.
//
// Kept as runtime flags rather than compile-time switches so the Pi can be
// moved between calibrating and running the show without a rebuild, and so
// only one binary ever exists. Without any flag nothing changes: no window
// is created, so ofApp::draw() and DebugView are never reached, and
// PreviewSender is never set up so its send() returns immediately.
//
// VERIFY ON THE PI: the ofSetupOpenGL/ofRunApp call shapes below are
// openFrameworks' long-standing single-window API; confirm the exact
// spellings against the installed 0.12.1 checkout - this cannot be checked
// from the Mac (no local OF install here).
int main(int argc, char * argv[]) {
	bool debugEnabled = false;
	bool previewEnabled = false;
	bool calibrationEnabled = false;

	for (int i = 1; i < argc; i++) {
		std::string argument = argv[i];
		if (argument == "--debug") {
			debugEnabled = true;
		} else if (argument == "--preview") {
			previewEnabled = true;
		} else if (argument == "--calibration") {
			calibrationEnabled = true;
			previewEnabled = true;
		} else {
			std::cerr << "sensorApp: unknown argument: " << argument << std::endl;
			std::cerr << "Usage: sensorApp [--debug] [--preview] [--calibration]" << std::endl;
			return EXIT_FAILURE;
		}
	}

	if (debugEnabled) {
		ofSetupOpenGL(DEBUG_WINDOW_WIDTH, DEBUG_WINDOW_HEIGHT, OF_WINDOW);
		ofRunApp(new ofApp(true, previewEnabled, calibrationEnabled));
	} else {
		ofAppNoWindow window;
		ofSetupOpenGL(&window, 1, 1, OF_WINDOW);
		ofRunApp(new ofApp(false, previewEnabled, calibrationEnabled));
	}

	return EXIT_SUCCESS;
}
