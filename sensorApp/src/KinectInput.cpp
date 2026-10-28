#include "KinectInput.h"
#include <algorithm>
#include <cstddef>
#include <sstream>
#include <thread>

// What KinectInput currently exposes:
//
// - isConnected()    -> bool, true while a device is open and started.
// - update()         -> FrameStatus, explicitly distinguishes a fresh frame
//                       from a timeout or a disconnected/reconnecting device.
// - getColorPixels() -> ofPixels, RGB, 1920x1080. Converted per-frame from the
//                       sensor's native BGRX layout.
// - getDepthPixels() -> ofFloatPixels, 1 channel, 512x424. Per-pixel distance
//                       from the sensor in millimeters (raw, unfiltered).
// - getIrPixels()    -> ofFloatPixels, 1 channel, 512x424. Raw infrared
//                       intensity, same resolution/alignment as depth.

namespace {
constexpr int frameWaitTimeoutMs = 250;
constexpr unsigned int timeoutsBeforeReconnect = 4;
constexpr long long initialReconnectDelayMs = 1000;
constexpr long long maximumReconnectDelayMs = 30000;
constexpr long long disconnectedPollDelayMs = 50;

// waitForNewFrame() transfers ownership of every returned Frame to the
// caller. Keeping that rule in a scope guard makes malformed-frame and future
// early-return paths release the whole map as reliably as the happy path.
class FrameMapGuard {
public:
    FrameMapGuard(libfreenect2::SyncMultiFrameListener * listener,
        libfreenect2::FrameMap & frames)
        : listener(listener)
        , frames(frames) {
    }

    ~FrameMapGuard() {
        if (listener != nullptr) {
            listener->release(frames);
        }
    }

    FrameMapGuard(const FrameMapGuard &) = delete;
    FrameMapGuard & operator=(const FrameMapGuard &) = delete;

private:
    libfreenect2::SyncMultiFrameListener * listener;
    libfreenect2::FrameMap & frames;
};

bool isUsableFrame(const libfreenect2::Frame * frame, size_t minimumBytesPerPixel) {
    return frame != nullptr
        && frame->data != nullptr
        && frame->width > 0
        && frame->height > 0
        && frame->bytes_per_pixel >= minimumBytesPerPixel
        && frame->status == 0;
}
}

KinectInput::~KinectInput() {
    exit();
}


bool KinectInput::setup() {
    closeDevice();
    running = true;
    frameFresh = false;
    consecutiveFrameTimeouts = 0;
    reconnectAttempt = 0;
    reconnectDelay = std::chrono::milliseconds(initialReconnectDelayMs);
    nextReconnectAttempt = std::chrono::steady_clock::now();

    // A missing Kinect is recoverable: setup reports the current state, while
    // update() keeps trying according to the bounded reconnect schedule.
    return attemptConnection();
}

bool KinectInput::openDevice(std::string & failureReason) {
    depthFocalLengthX = defaultDepthFocalLength;
    depthFocalLengthY = defaultDepthFocalLength;

    if (freenect2.enumerateDevices() == 0) {
        failureReason = "no Kinect v2 device found (check power adapter + USB 3.0 port)";
        return false;
    }

    std::string serial = freenect2.getDefaultDeviceSerialNumber();
    if (serial.empty()) {
        failureReason = "Kinect enumeration returned no usable serial number";
        return false;
    }

    // same pipeline selection as libfreenect2's Protonect example: prefer the
    // GL backend (the Jyurineko fork builds this against OpenGL ES 3.1 on the
    // Pi 4, verified working via ./Protonect gl), fall back to CPU otherwise
#ifdef LIBFREENECT2_WITH_OPENGL_SUPPORT
    libfreenect2::PacketPipeline * pipeline = new libfreenect2::OpenGLPacketPipeline();
#else
    libfreenect2::PacketPipeline * pipeline = new libfreenect2::CpuPacketPipeline();
#endif

    // openDevice() always takes ownership of pipeline, including on failure.
    // Never delete or reuse it here; each reconnect creates a fresh instance.
    device = freenect2.openDevice(serial, pipeline);
    if (!device) {
        failureReason = "could not open Kinect with serial " + serial;
        return false;
    }

    listener = new libfreenect2::SyncMultiFrameListener(
        libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);

    device->setColorFrameListener(listener);
    device->setIrAndDepthFrameListener(listener);

    if (!device->start()) {
        failureReason = "Kinect " + serial + " was found but failed to start its streams";
        closeDevice();
        return false;
    }

    // Factory calibration of the depth (IR) camera. Only fx/fy are kept - the
    // principal point and distortion coefficients would matter for
    // reprojecting points into 3D, but converting a pixel span into a length
    // at a known distance needs only the focal length.
    //
    // VERIFY ON THE PI: getIrCameraParams() is libfreenect2's long-standing
    // accessor, but it must be called after start() (the values come off the
    // device) - confirm the returned fx/fy are plausible, around 365 for a
    // Kinect v2, rather than zero.
    libfreenect2::Freenect2Device::IrCameraParams irParams = device->getIrCameraParams();
    if (irParams.fx > 0.0f && irParams.fy > 0.0f) {
        depthFocalLengthX = irParams.fx;
        depthFocalLengthY = irParams.fy;
    } else {
        ofLogWarning("KinectInput") << "device reported no usable IR calibration - falling back to "
                                    << defaultDepthFocalLength << "px focal length, metric sizes will be approximate";
    }

    ofLogNotice("KinectInput") << "connected: serial " << device->getSerialNumber()
                                << ", firmware " << device->getFirmwareVersion()
                                << ", depth focal length " << depthFocalLengthX << "x" << depthFocalLengthY << "px";

    connected = true;
    frameFresh = false;
    consecutiveFrameTimeouts = 0;
    connectionGeneration++;
    return true;
}

bool KinectInput::attemptConnection() {
    if (!running) {
        return false;
    }

    reconnectAttempt++;

    std::string failureReason;
    if (openDevice(failureReason)) {
        ofLogNotice("KinectInput") << "Kinect streams started on connection attempt " << reconnectAttempt;
        return true;
    }

    scheduleReconnect(failureReason);
    return false;
}

void KinectInput::scheduleReconnect(const std::string & reason) {
    closeDevice();
    if (!running) {
        return;
    }

    nextReconnectAttempt = std::chrono::steady_clock::now() + reconnectDelay;

    ofLogWarning("KinectInput") << reason
        << "; reconnect attempt " << (reconnectAttempt + 1)
        << " in " << reconnectDelay.count() << " ms";

    reconnectDelay = std::chrono::milliseconds(
        std::min<long long>(
            static_cast<long long>(reconnectDelay.count()) * 2,
            maximumReconnectDelayMs));
}

KinectInput::FrameStatus KinectInput::update() {
    frameFresh = false;

    if (!running) {
        return FrameStatus::Disconnected;
    }

    if (!connected) {
        auto now = std::chrono::steady_clock::now();
        if (now < nextReconnectAttempt) {
            // ofAppNoWindow is not paced by VSync. Without this small bounded
            // wait, the app would busy-spin on one CPU core throughout a
            // potentially 30-second reconnect backoff.
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                nextReconnectAttempt - now);
            auto pollDelay = std::min(
                remaining + std::chrono::milliseconds(1),
                std::chrono::milliseconds(disconnectedPollDelayMs));
            std::this_thread::sleep_for(pollDelay);
            return FrameStatus::Disconnected;
        }
        if (!attemptConnection()) {
            return FrameStatus::Disconnected;
        }
    }

    libfreenect2::FrameMap frames;
    // A short bounded wait keeps the app/reconnect loop responsive. Four
    // consecutive misses still allow a full second for a transient USB stall
    // before the device is torn down and re-enumerated.
    if (!listener->waitForNewFrame(frames, frameWaitTimeoutMs)) {
        consecutiveFrameTimeouts++;

        if (consecutiveFrameTimeouts == 1) {
            ofLogWarning("KinectInput") << "no fresh Kinect frame within "
                << frameWaitTimeoutMs << " ms; stale pixels are being withheld";
        }

        if (consecutiveFrameTimeouts >= timeoutsBeforeReconnect) {
            std::ostringstream reason;
            reason << "Kinect produced no fresh frame for "
                << (consecutiveFrameTimeouts * frameWaitTimeoutMs)
                << " ms; closing the stalled connection";
            scheduleReconnect(reason.str());
            return FrameStatus::Disconnected;
        }

        return FrameStatus::Timeout;
    }

    std::string invalidFrameReason;
    {
        FrameMapGuard frameGuard(listener, frames);

        auto colorEntry = frames.find(libfreenect2::Frame::Color);
        auto depthEntry = frames.find(libfreenect2::Frame::Depth);
        auto irEntry = frames.find(libfreenect2::Frame::Ir);

        libfreenect2::Frame * colorFrame = colorEntry != frames.end() ? colorEntry->second : nullptr;
        libfreenect2::Frame * depthFrame = depthEntry != frames.end() ? depthEntry->second : nullptr;
        libfreenect2::Frame * irFrame = irEntry != frames.end() ? irEntry->second : nullptr;

        bool supportedColorFormat = colorFrame != nullptr
            && (colorFrame->format == libfreenect2::Frame::BGRX
                || colorFrame->format == libfreenect2::Frame::RGBX);

        if (!isUsableFrame(colorFrame, 4) || !supportedColorFormat) {
            invalidFrameReason = "Kinect returned a missing or malformed color frame";
        } else if (!isUsableFrame(depthFrame, sizeof(float))
            || depthFrame->format != libfreenect2::Frame::Float) {
            invalidFrameReason = "Kinect returned a missing or malformed depth frame";
        } else if (!isUsableFrame(irFrame, sizeof(float))
            || irFrame->format != libfreenect2::Frame::Float) {
            invalidFrameReason = "Kinect returned a missing or malformed IR frame";
        } else {
            // Color normally arrives as BGRX, while some decoders report
            // RGBX. Normalize either supported format to RGB ofPixels.
            colorPixels.allocate(colorFrame->width, colorFrame->height, OF_PIXELS_RGB);
            const unsigned char * src = colorFrame->data;
            unsigned char * dst = colorPixels.getData();
            size_t pixelCount = static_cast<size_t>(colorFrame->width) * colorFrame->height;
            bool sourceIsBgr = colorFrame->format == libfreenect2::Frame::BGRX;
            for (size_t i = 0; i < pixelCount; i++) {
                dst[i * 3 + 0] = src[i * 4 + (sourceIsBgr ? 2 : 0)]; // R
                dst[i * 3 + 1] = src[i * 4 + 1]; // G
                dst[i * 3 + 2] = src[i * 4 + (sourceIsBgr ? 0 : 2)]; // B
            }

            // depth (mm) and ir frames both arrive as single-channel float data
            depthPixels.setFromPixels(reinterpret_cast<float *>(depthFrame->data),
                depthFrame->width, depthFrame->height, 1);
            irPixels.setFromPixels(reinterpret_cast<float *>(irFrame->data),
                irFrame->width, irFrame->height, 1);
        }
    }

    if (!invalidFrameReason.empty()) {
        scheduleReconnect(invalidFrameReason + "; closing the invalid stream");
        return FrameStatus::Disconnected;
    }

    if (consecutiveFrameTimeouts > 0) {
        ofLogNotice("KinectInput") << "fresh Kinect frames resumed after "
            << consecutiveFrameTimeouts << " timed wait(s)";
    }

    consecutiveFrameTimeouts = 0;
    reconnectAttempt = 0;
    reconnectDelay = std::chrono::milliseconds(initialReconnectDelayMs);
    depthFrameSequence++;
    frameFresh = true;
    return FrameStatus::FreshFrame;
}

void KinectInput::closeDevice() {
    bool wasConnected = connected;
    frameFresh = false;
    connected = false;
    consecutiveFrameTimeouts = 0;

    // Do not leave a previously valid buffer looking usable after a
    // disconnect. This is defense in depth in addition to FrameStatus.
    colorPixels.clear();
    depthPixels.clear();
    irPixels.clear();

    // Follow libfreenect2's documented stop/close order first. The exact
    // Jyurineko fork used by this project does not destroy the object in
    // close(): its destructor additionally unregisters it from the Freenect2
    // context and frees the PacketPipeline consumed by openDevice(). Hence
    // delete is required after close() for repeated reconnects not to leak.
    if (device) {
        if (wasConnected && !device->stop()) {
            ofLogWarning("KinectInput") << "Kinect stream stop reported a failure during cleanup";
        }
        if (!device->close()) {
            ofLogWarning("KinectInput") << "Kinect device close reported a failure during cleanup";
        }
        delete device;
        device = nullptr;
    }

    delete listener;
    listener = nullptr;
}

void KinectInput::exit() {
    running = false;
    closeDevice();
}
