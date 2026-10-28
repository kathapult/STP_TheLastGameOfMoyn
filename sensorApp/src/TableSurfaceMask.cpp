#include "TableSurfaceMask.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
constexpr std::array<char, 8> fileMagic {{ 'T', 'L', 'G', 'O', 'M', 'T', 'M', '1' }};
constexpr uint32_t fileVersion = 1;
static_assert(sizeof(float) == 4, "table mask format requires 32-bit floats");

uint32_t checksum(const std::vector<float> & values) {
	// FNV-1a catches truncated and accidentally modified site files without
	// introducing a platform-specific or external checksum dependency.
	uint32_t hash = 2166136261u;
	const unsigned char * bytes = reinterpret_cast<const unsigned char *>(values.data());
	const size_t byteCount = values.size() * sizeof(float);
	for (size_t index = 0; index < byteCount; ++index) {
		hash ^= bytes[index];
		hash *= 16777619u;
	}
	return hash;
}

bool writeU32(std::ofstream & output, uint32_t value) {
	output.write(reinterpret_cast<const char *>(&value), sizeof(value));
	return static_cast<bool>(output);
}

bool readU32(std::ifstream & input, uint32_t & value) {
	input.read(reinterpret_cast<char *>(&value), sizeof(value));
	return static_cast<bool>(input);
}
}

bool TableSurfaceMask::setup(const std::string & path) {
	storagePath = path;
	return loadFromDisk();
}

bool TableSurfaceMask::reload() {
	resetCaptureBuffers();
	return loadFromDisk();
}

bool TableSurfaceMask::loadFromDisk() {
	if (storagePath.empty()) {
		failCapture("table mask path is empty");
		return false;
	}

	std::error_code fileError;
	if (!std::filesystem::exists(storagePath, fileError)) {
		if (fileError) {
			failCapture("could not inspect table mask file: " + fileError.message());
			return false;
		}
		surfaceDepthMM.clear();
		surfaceWidth = 0;
		surfaceHeight = 0;
		surfaceRoiX = 0;
		surfaceRoiY = 0;
		surfaceRoiWidth = 0;
		surfaceRoiHeight = 0;
		validCoverage = 0.0f;
		captureState = CaptureState::Missing;
		lastError.clear();
		return true;
	}

	std::ifstream input(storagePath, std::ios::binary);
	if (!input) {
		failCapture("could not open table mask file");
		return false;
	}

	std::array<char, fileMagic.size()> magic {};
	input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
	uint32_t version = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t roiX = 0;
	uint32_t roiY = 0;
	uint32_t roiWidth = 0;
	uint32_t roiHeight = 0;
	uint32_t storedValidCount = 0;
	uint32_t storedChecksum = 0;
	if (!input || magic != fileMagic
		|| !readU32(input, version)
		|| !readU32(input, width) || !readU32(input, height)
		|| !readU32(input, roiX) || !readU32(input, roiY)
		|| !readU32(input, roiWidth) || !readU32(input, roiHeight)
		|| !readU32(input, storedValidCount)
		|| !readU32(input, storedChecksum)
		|| version != fileVersion || width == 0 || height == 0
		|| width > static_cast<uint32_t>(maximumDimension)
		|| height > static_cast<uint32_t>(maximumDimension)
		|| roiWidth == 0 || roiHeight == 0 || roiX >= width || roiY >= height
		|| roiWidth > width - roiX || roiHeight > height - roiY) {
		failCapture("table mask header is invalid");
		return false;
	}

	const size_t pixelCount = static_cast<size_t>(width) * height;
	std::vector<float> candidate(pixelCount, 0.0f);
	input.read(reinterpret_cast<char *>(candidate.data()),
		static_cast<std::streamsize>(pixelCount * sizeof(float)));
	char trailing = 0;
	if (!input || input.read(&trailing, 1)
		|| checksum(candidate) != storedChecksum) {
		failCapture("table mask payload is incomplete or corrupt");
		return false;
	}

	size_t validCount = 0;
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const float value = candidate[static_cast<size_t>(y) * width + x];
			if (!std::isfinite(value) || value < 0.0f
				|| value > maximumStoredDepthMM) {
				failCapture("table mask contains an invalid depth value");
				return false;
			}
			if (value <= 0.0f) continue;
			if (x < roiX || x >= roiX + roiWidth
				|| y < roiY || y >= roiY + roiHeight) {
				failCapture("table mask contains depth outside its saved ROI");
				return false;
			}
			validCount++;
		}
	}
	if (validCount != storedValidCount) {
		failCapture("table mask valid-pixel count does not match its header");
		return false;
	}

	const size_t roiPixelCount = static_cast<size_t>(roiWidth) * roiHeight;
	const float coverage = roiPixelCount > 0
		? static_cast<float>(validCount) / static_cast<float>(roiPixelCount)
		: 0.0f;
	if (coverage < minimumValidCoverage) {
		failCapture("table mask does not cover enough of the saved ROI");
		return false;
	}

	surfaceDepthMM = std::move(candidate);
	surfaceWidth = static_cast<int>(width);
	surfaceHeight = static_cast<int>(height);
	surfaceRoiX = static_cast<int>(roiX);
	surfaceRoiY = static_cast<int>(roiY);
	surfaceRoiWidth = static_cast<int>(roiWidth);
	surfaceRoiHeight = static_cast<int>(roiHeight);
	validCoverage = coverage;
	captureState = CaptureState::Ready;
	lastError.clear();
	return true;
}

bool TableSurfaceMask::requestCapture() {
	if (captureState == CaptureState::Capturing || storagePath.empty()) return false;
	resetCaptureBuffers();
	captureState = CaptureState::Capturing;
	lastError.clear();
	return true;
}

bool TableSurfaceMask::addFrame(const float * depthMM, int width, int height,
	int roiX, int roiY, int roiWidth, int roiHeight) {
	if (captureState != CaptureState::Capturing) return false;
	if (depthMM == nullptr || width <= 0 || height <= 0
		|| width > maximumDimension || height > maximumDimension
		|| roiX < 0 || roiY < 0 || roiWidth <= 0 || roiHeight <= 0
		|| roiWidth > width || roiHeight > height
		|| roiX > width - roiWidth || roiY > height - roiHeight) {
		failCapture("capture frame geometry is invalid");
		return false;
	}

	if (captureSums.empty()) {
		captureWidth = width;
		captureHeight = height;
		captureRoiX = roiX;
		captureRoiY = roiY;
		captureRoiWidth = roiWidth;
		captureRoiHeight = roiHeight;
		const size_t pixelCount = static_cast<size_t>(width) * height;
		captureSums.assign(pixelCount, 0.0f);
		captureSamples.assign(pixelCount, 0);
	} else if (width != captureWidth || height != captureHeight
		|| roiX != captureRoiX || roiY != captureRoiY
		|| roiWidth != captureRoiWidth || roiHeight != captureRoiHeight) {
		failCapture("depth dimensions or ROI changed during table capture");
		return false;
	}

	for (int y = roiY; y < roiY + roiHeight; ++y) {
		for (int x = roiX; x < roiX + roiWidth; ++x) {
			const size_t index = static_cast<size_t>(y) * width + x;
			const float value = depthMM[index];
			if (std::isfinite(value) && value > 0.0f
				&& value <= maximumStoredDepthMM) {
				captureSums[index] += value;
				captureSamples[index]++;
			}
		}
	}
	capturedFrames++;
	if (capturedFrames < captureFrameCount) return true;

	const size_t pixelCount = static_cast<size_t>(width) * height;
	std::vector<float> candidate(pixelCount, 0.0f);
	const int minimumSamples = (captureFrameCount * 2 + 2) / 3;
	size_t validCount = 0;
	for (int y = roiY; y < roiY + roiHeight; ++y) {
		for (int x = roiX; x < roiX + roiWidth; ++x) {
			const size_t index = static_cast<size_t>(y) * width + x;
			if (captureSamples[index] < minimumSamples) continue;
			candidate[index] = captureSums[index]
				/ static_cast<float>(captureSamples[index]);
			validCount++;
		}
	}
	const size_t roiPixelCount = static_cast<size_t>(roiWidth) * roiHeight;
	const float coverage = static_cast<float>(validCount)
		/ static_cast<float>(roiPixelCount);
	if (coverage < minimumValidCoverage) {
		failCapture("less than 70 percent of the table ROI had stable depth data");
		return false;
	}
	if (!persist(candidate, width, height, roiX, roiY, roiWidth, roiHeight)) {
		failCapture("could not persist the captured table mask");
		return false;
	}

	surfaceDepthMM = std::move(candidate);
	surfaceWidth = width;
	surfaceHeight = height;
	surfaceRoiX = roiX;
	surfaceRoiY = roiY;
	surfaceRoiWidth = roiWidth;
	surfaceRoiHeight = roiHeight;
	validCoverage = coverage;
	resetCaptureBuffers();
	captureState = CaptureState::Ready;
	lastError.clear();
	return true;
}

bool TableSurfaceMask::persist(const std::vector<float> & candidate,
	int width, int height, int roiX, int roiY, int roiWidth, int roiHeight) {
	const std::filesystem::path target(storagePath);
	const std::filesystem::path temporary = target.string() + ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output) return false;

	const uint32_t validCount = static_cast<uint32_t>(std::count_if(
		candidate.begin(), candidate.end(), [](float value) { return value > 0.0f; }));
	output.write(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
	const bool headerWritten = writeU32(output, fileVersion)
		&& writeU32(output, static_cast<uint32_t>(width))
		&& writeU32(output, static_cast<uint32_t>(height))
		&& writeU32(output, static_cast<uint32_t>(roiX))
		&& writeU32(output, static_cast<uint32_t>(roiY))
		&& writeU32(output, static_cast<uint32_t>(roiWidth))
		&& writeU32(output, static_cast<uint32_t>(roiHeight))
		&& writeU32(output, validCount)
		&& writeU32(output, checksum(candidate));
	if (headerWritten) {
		output.write(reinterpret_cast<const char *>(candidate.data()),
			static_cast<std::streamsize>(candidate.size() * sizeof(float)));
	}
	output.close();
	if (!headerWritten || !output) {
		std::error_code removeError;
		std::filesystem::remove(temporary, removeError);
		return false;
	}

	// Pi A and macOS provide atomic replacement when both names share a
	// directory. The temporary file is never treated as an active mask.
	if (std::rename(temporary.string().c_str(), target.string().c_str()) != 0) {
		std::error_code removeError;
		std::filesystem::remove(temporary, removeError);
		return false;
	}
	return true;
}

bool TableSurfaceMask::clear() {
	if (storagePath.empty()) return false;
	std::error_code removeError;
	const bool exists = std::filesystem::exists(storagePath, removeError);
	if (removeError) {
		captureState = CaptureState::Error;
		lastError = "could not inspect table mask before deletion: "
			+ removeError.message();
		return false;
	}
	if (exists) {
		std::filesystem::remove(storagePath, removeError);
		if (removeError) {
			captureState = CaptureState::Error;
			lastError = "could not delete table mask: " + removeError.message();
			return false;
		}
	}

	surfaceDepthMM.clear();
	surfaceWidth = 0;
	surfaceHeight = 0;
	surfaceRoiX = 0;
	surfaceRoiY = 0;
	surfaceRoiWidth = 0;
	surfaceRoiHeight = 0;
	validCoverage = 0.0f;
	resetCaptureBuffers();
	captureState = CaptureState::Missing;
	lastError.clear();
	return true;
}

bool TableSurfaceMask::isForeground(float currentDepthMM, int x, int y,
	float clearanceMM) const {
	const float reference = referenceDepthAt(x, y);
	return reference > 0.0f && std::isfinite(currentDepthMM)
		&& currentDepthMM > 0.0f
		&& currentDepthMM + std::max(0.0f, clearanceMM) < reference;
}

float TableSurfaceMask::referenceDepthAt(int x, int y) const {
	if (surfaceDepthMM.empty() || x < 0 || y < 0
		|| x >= surfaceWidth || y >= surfaceHeight) return 0.0f;
	return surfaceDepthMM[static_cast<size_t>(y) * surfaceWidth + x];
}

TableSurfaceMask::Status TableSurfaceMask::getStatus() const {
	Status status;
	status.active = isActive();
	status.captureState = captureState;
	status.capturedFrames = capturedFrames;
	status.requiredFrames = captureFrameCount;
	status.validCoverage = validCoverage;
	return status;
}

void TableSurfaceMask::failCapture(const std::string & message) {
	resetCaptureBuffers();
	captureState = CaptureState::Error;
	lastError = message;
}

void TableSurfaceMask::resetCaptureBuffers() {
	capturedFrames = 0;
	captureSums.clear();
	captureSamples.clear();
	captureWidth = 0;
	captureHeight = 0;
	captureRoiX = 0;
	captureRoiY = 0;
	captureRoiWidth = 0;
	captureRoiHeight = 0;
}
