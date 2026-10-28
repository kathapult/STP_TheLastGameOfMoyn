#include "TableSurfaceMask.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main() {
	const std::string unique = "thelastgameofmoyn-table-mask-"
		+ std::to_string(std::chrono::high_resolution_clock::now()
			.time_since_epoch().count()) + ".bin";
	const std::filesystem::path path = std::filesystem::temp_directory_path()
		/ unique;
	std::error_code cleanupError;
	std::filesystem::remove(path, cleanupError);

	constexpr int width = 6;
	constexpr int height = 4;
	constexpr int roiX = 1;
	constexpr int roiY = 1;
	constexpr int roiWidth = 4;
	constexpr int roiHeight = 2;
	std::vector<float> depth(static_cast<size_t>(width) * height, 0.0f);
	for (int y = roiY; y < roiY + roiHeight; ++y) {
		for (int x = roiX; x < roiX + roiWidth; ++x) {
			depth[static_cast<size_t>(y) * width + x] = 1000.0f
				+ static_cast<float>(x * 3 + y);
		}
	}
	// One stable Kinect dropout still leaves 87.5% valid ROI coverage.
	depth[static_cast<size_t>(roiY) * width + roiX] = 0.0f;

	TableSurfaceMask mask;
	assert(mask.setup(path.string()));
	assert(!mask.isActive());
	assert(mask.requestCapture());
	assert(!mask.requestCapture());
	for (int frame = 0; frame < TableSurfaceMask::captureFrameCount; ++frame) {
		std::vector<float> sample = depth;
		for (float & value : sample) {
			if (value > 0.0f) value += static_cast<float>((frame % 3) - 1);
		}
		assert(mask.addFrame(sample.data(), width, height,
			roiX, roiY, roiWidth, roiHeight));
	}

	const TableSurfaceMask::Status ready = mask.getStatus();
	assert(ready.active);
	assert(ready.captureState == TableSurfaceMask::CaptureState::Ready);
	assert(std::fabs(ready.validCoverage - 0.875f) < 0.0001f);
	assert(!mask.isForeground(900.0f, roiX, roiY, 15.0f));
	const int validX = roiX + 1;
	const float reference = mask.referenceDepthAt(validX, roiY);
	assert(reference > 1000.0f);
	assert(mask.isForeground(reference - 20.0f, validX, roiY, 15.0f));
	assert(!mask.isForeground(reference - 10.0f, validX, roiY, 15.0f));
	assert(!mask.isForeground(900.0f, 0, 0, 15.0f));

	TableSurfaceMask loaded;
	assert(loaded.setup(path.string()));
	assert(loaded.isActive());
	assert(std::fabs(loaded.referenceDepthAt(validX, roiY) - reference) < 0.001f);

	// The checksum protects the payload; independent header validation must
	// also reject a saved ROI that no longer contains all non-zero references.
	const std::filesystem::path invalidRoiPath = path.string() + ".invalid-roi";
	std::filesystem::copy_file(path, invalidRoiPath,
		std::filesystem::copy_options::overwrite_existing);
	std::fstream invalidRoiFile(invalidRoiPath,
		std::ios::binary | std::ios::in | std::ios::out);
	assert(invalidRoiFile);
	constexpr uint32_t shrunkRoiWidth = 3;
	constexpr std::streamoff roiWidthOffset = 8 + 5 * sizeof(uint32_t);
	invalidRoiFile.seekp(roiWidthOffset);
	invalidRoiFile.write(reinterpret_cast<const char *>(&shrunkRoiWidth),
		sizeof(shrunkRoiWidth));
	invalidRoiFile.close();
	TableSurfaceMask invalidRoi;
	assert(!invalidRoi.setup(invalidRoiPath.string()));
	assert(!invalidRoi.isActive());
	assert(invalidRoi.getStatus().captureState
		== TableSurfaceMask::CaptureState::Error);
	std::filesystem::remove(invalidRoiPath, cleanupError);

	// A failed recapture must not discard the last known-good active model.
	std::fill(depth.begin(), depth.end(), 0.0f);
	for (int x = roiX; x < roiX + 2; ++x) {
		for (int y = roiY; y < roiY + roiHeight; ++y) {
			depth[static_cast<size_t>(y) * width + x] = 1000.0f;
		}
	}
	assert(loaded.requestCapture());
	for (int frame = 0; frame < TableSurfaceMask::captureFrameCount - 1; ++frame) {
		assert(loaded.addFrame(depth.data(), width, height,
			roiX, roiY, roiWidth, roiHeight));
	}
	assert(!loaded.addFrame(depth.data(), width, height,
		roiX, roiY, roiWidth, roiHeight));
	assert(loaded.isActive());
	assert(loaded.getStatus().captureState == TableSurfaceMask::CaptureState::Error);
	assert(std::fabs(loaded.referenceDepthAt(validX, roiY) - reference) < 0.001f);

	assert(loaded.clear());
	assert(!loaded.isActive());
	assert(!std::filesystem::exists(path));

	std::cout << "table surface mask tests passed\n";
	return 0;
}
