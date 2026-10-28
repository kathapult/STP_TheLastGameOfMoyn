# Attempt to load a config.make file.
# If none is found, project defaults in config.project.make will be used.
ifneq ($(wildcard config.make),)
	include config.make
endif

# make sure the the OF_ROOT location is defined
ifndef OF_ROOT
	OF_ROOT=../../../..
endif

# call the project makefile!
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk

# Builds and runs the RELEASE binary with the --debug flag, i.e. with the
# calibration window (DebugView) instead of windowless operation - see
# sensorApp/src/main.cpp. Plain `make RunRelease` still starts the show
# configuration with no window.
#
# Deliberately NOT named RunDebug: openFrameworks already defines that target
# with a different meaning (run the DEBUG *build*, bin/$(APPNAME)_debug), and
# --debug is orthogonal to which build configuration is running. Redefining it
# would shadow OF's recipe and silently change what `make RunDebug` does.
#
# Mirrors OF's own Run targets by cd'ing into bin/ first, so the binary finds
# bin/data/ relative to its working directory.
#
# The binary name falls back to this project folder's own name, which is what
# OF's APPNAME defaults to anyway - that keeps the target working even if the
# installed OF version spells the variable differently (BIN_NAME/TARGET_NAME
# have both been used across releases), and this cannot be checked from the
# Mac checkout.
DEBUG_VIEW_BIN = $(if $(APPNAME),$(APPNAME),$(notdir $(CURDIR)))

RunDebugView: Release
	@cd bin && ./$(DEBUG_VIEW_BIN) --debug

# Same idea for a Pi A with no monitor: stays windowless, but streams a small
# copy of the mask to Pi B, which draws it in the corner. This is the usual
# development mode here, since Pi A normally has no screen attached.
RunPreview: Release
	@cd bin && ./$(DEBUG_VIEW_BIN) --preview

.PHONY: RunDebugView RunPreview
