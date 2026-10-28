#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Pi A's persistent per-pixel depth reference for the empty table. A current
// pixel is foreground only when it is measurably closer to the Kinect than
// the captured surface at the same position. This removes a sloped or uneven
// table without forcing the global far-depth threshold to stop above its
// nearest point.
class TableSurfaceMask {
public:
	enum class CaptureState : int32_t {
		Missing = 0,
		Capturing = 1,
		Ready = 2,
		Error = 3
	};

	struct Status {
		bool active = false;
		CaptureState captureState = CaptureState::Missing;
		int capturedFrames = 0;
		int requiredFrames = 0;
		float validCoverage = 0.0f;
	};

	// The path is resolved by ObjectDetector next to the persistent
	// settings.json target, not inside an individual release directory.
	bool setup(const std::string & path);
	bool reload();

	// Starts a short temporal average. The first subsequent detector frame
	// fixes the dimensions and ROI; further frames must match it.
	bool requestCapture();
	bool addFrame(const float * depthMM, int width, int height,
		int roiX, int roiY, int roiWidth, int roiHeight);

	// Removes both the active reference and its persistent file. Returns false
	// without dropping the active in-memory reference if deletion fails.
	bool clear();

	bool isActive() const { return !surfaceDepthMM.empty(); }
	bool isCapturing() const { return captureState == CaptureState::Capturing; }
	bool matchesDimensions(int width, int height) const {
		return !isActive() || (width == surfaceWidth && height == surfaceHeight);
	}
	bool isForeground(float currentDepthMM, int x, int y,
		float clearanceMM) const;
	float referenceDepthAt(int x, int y) const;
	Status getStatus() const;
	const std::string & getLastError() const { return lastError; }

	static constexpr int captureFrameCount = 15;

private:
	bool loadFromDisk();
	bool persist(const std::vector<float> & candidate, int width, int height,
		int roiX, int roiY, int roiWidth, int roiHeight);
	void failCapture(const std::string & message);
	void resetCaptureBuffers();

	std::string storagePath;
	std::vector<float> surfaceDepthMM;
	int surfaceWidth = 0;
	int surfaceHeight = 0;
	int surfaceRoiX = 0;
	int surfaceRoiY = 0;
	int surfaceRoiWidth = 0;
	int surfaceRoiHeight = 0;
	float validCoverage = 0.0f;

	CaptureState captureState = CaptureState::Missing;
	int capturedFrames = 0;
	std::vector<float> captureSums;
	std::vector<uint16_t> captureSamples;
	int captureWidth = 0;
	int captureHeight = 0;
	int captureRoiX = 0;
	int captureRoiY = 0;
	int captureRoiWidth = 0;
	int captureRoiHeight = 0;
	std::string lastError;

	static constexpr float minimumValidCoverage = 0.70f;
	static constexpr float maximumStoredDepthMM = 10000.0f;
	static constexpr int maximumDimension = 4096;
};
