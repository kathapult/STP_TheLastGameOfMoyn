################################################################################
# CONFIGURE PROJECT MAKEFILE (optional)
#   This file is where we make project specific configurations.
################################################################################

################################################################################
# OF ROOT
#   The location of your root openFrameworks installation
#       (default) OF_ROOT = ../../..
################################################################################
# Mac (dev/edit-only) has OF checked out relative to this project; the Pi's
# install lives at a fixed absolute path instead - see CLAUDE.md.
ifeq ($(shell uname -s), Linux)
	OF_ROOT = /opt/of_v0.12.1_linuxaarch64_release
else
	OF_ROOT = ../../..
endif

################################################################################
# PROJECT EXTERNAL SOURCE PATHS
#   shared/ holds DetectedObject.h (the one type this project and visualApp/
#   agree on), TableShape.h (their common physical table outline) and
#   OneEuroFilter.h (the smoothing filter both projects' tracking code is
#   built on - see processDocumentation.md). It lives
#   outside this project's own directory tree (PROJECT_ROOT = sensorApp/),
#   so it must be pulled in explicitly here - the root project auto-includes
#   it instead, since shared/ sits inside its own tree. See research.md
#   item 3.
################################################################################
PROJECT_EXTERNAL_SOURCE_PATHS += ../shared

################################################################################
# PROJECT LINKER FLAGS
################################################################################

# Kinect v2 (libfreenect2, direct link, no OF addon) - Pi/Linux only, see
# kinect-v2-raspberry-pi-setup.md. Installed via `sudo make install` there
# (headers in /usr/local/include/libfreenect2, lib in /usr/local/lib).
# Guarded even though this project is never built on Mac in practice - see
# research.md item 6 (mirrors the same guard on the root project).
ifeq ($(shell uname -s), Linux)
	PROJECT_LDFLAGS += -lfreenect2
	PROJECT_CFLAGS += -I/usr/local/include/libfreenect2
endif

# osx template

# Uncomment/comment below to switch between C++11 and C++17 ( or newer ). On macOS C++17 needs 10.15 or above.
# export MAC_OS_MIN_VERSION = 10.15
# export MAC_OS_CPP_VER = -std=c++17
