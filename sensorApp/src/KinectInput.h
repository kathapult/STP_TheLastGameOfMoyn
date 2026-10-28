#pragma once
#include "ofMain.h"
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/packet_pipeline.h>
#include <chrono>
#include <cstdint>
#include <string>

// Kinect v2 stream input via libfreenect2, linked directly (no OF addon -
// ofxKinect is v1-only, ofxKinectV2/ofxLibfreenect2 require real OpenCL which
// the Pi 4's GPU doesn't have). See kinect-v2-raspberry-pi-setup.md for the
// driver/build side (Jyurineko fork, OpenGL ES 3.1 backend).
//
// Exposes the raw color/depth/ir streams only. Hand/gesture extraction is a
// later CV step built on top of these frames (see HandData/GestureDetector).
class KinectInput {
public:
    enum class FrameStatus {
        FreshFrame,
        Timeout,
        Disconnected
    };

    KinectInput() = default;
    ~KinectInput();

    KinectInput(const KinectInput &) = delete;
    KinectInput & operator=(const KinectInput &) = delete;

    bool setup();
    FrameStatus update();
    void exit();

    bool isConnected() const { return connected; }

    // True only between a successful update() and the next update() call.
    // Callers must not mistake the last allocated pixel buffer for a new
    // measurement after a timeout.
    bool hasFreshDepthFrame() const { return frameFresh; }

    // Monotonically increasing application-side sequence. Unlike the
    // device's own counter this does not restart when the Kinect reconnects.
    std::uint64_t getDepthFrameSequence() const { return depthFrameSequence; }

    // Incremented whenever a device has been opened and started again. The
    // detector uses this to discard temporal state from the old connection.
    std::uint64_t getConnectionGeneration() const { return connectionGeneration; }

    const ofPixels & getColorPixels() const { return colorPixels; }
    const ofFloatPixels & getDepthPixels() const { return depthPixels; }
    const ofFloatPixels & getIrPixels() const { return irPixels; }

    // Focal length of the depth camera in pixels, read from the device's own
    // factory calibration. This is what converts a pixel count into a real
    // length: an object d millimetres away covers
    //   pixels = millimetres * focalLength / d
    // so the same hand measures fewer pixels the higher it is held. Without
    // this, every size threshold silently depends on how high the sensor
    // hangs and how high the object is lifted.
    float getDepthFocalLengthX() const { return depthFocalLengthX; }
    float getDepthFocalLengthY() const { return depthFocalLengthY; }

private:
    bool openDevice(std::string & failureReason);
    void closeDevice();
    bool attemptConnection();
    void scheduleReconnect(const std::string & reason);

    // Nominal Kinect v2 depth focal length, used until the device reports its
    // own. Every unit is calibrated slightly differently, so this is a
    // fallback for the no-device case, not a substitute.
    static constexpr float defaultDepthFocalLength = 365.0f;

    float depthFocalLengthX = defaultDepthFocalLength;
    float depthFocalLengthY = defaultDepthFocalLength;

    libfreenect2::Freenect2 freenect2;
    libfreenect2::Freenect2Device * device = nullptr;
    libfreenect2::SyncMultiFrameListener * listener = nullptr;

    ofPixels colorPixels;
    ofFloatPixels depthPixels;
    ofFloatPixels irPixels;

    bool running = false;
    bool connected = false;
    bool frameFresh = false;

    unsigned int consecutiveFrameTimeouts = 0;
    unsigned int reconnectAttempt = 0;
    std::chrono::milliseconds reconnectDelay { 1000 };
    std::chrono::steady_clock::time_point nextReconnectAttempt;

    std::uint64_t depthFrameSequence = 0;
    std::uint64_t connectionGeneration = 0;
};
