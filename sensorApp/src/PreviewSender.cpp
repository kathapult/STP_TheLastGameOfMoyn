#include "PreviewSender.h"

void PreviewSender::setup(const std::string & host, int port) {
	sender.setup(host, port);
	enabled = true;
	ofLogNotice("PreviewSender") << "mask preview streaming to " << host << ":" << port
								 << " (1/" << downscale << " scale, every " << sendEveryNthFrame << " frames)";
}

void PreviewSender::send(const cv::Mat & mask, const ofFloatPixels & depth,
	const std::vector<CalibrationProtocol::TrackDiagnostic> & tracks,
	const ObjectStreamProtocol::FrameIdentity & frame) {
	if (!enabled || mask.empty() || !depth.isAllocated()
		|| depth.getNumChannels() != 1 || depth.getData() == nullptr) {
		return;
	}

	// Throttled rather than sent every frame: a preview is for judging a
	// threshold by eye, which 10 fps does as well as 30 at a third of the
	// bandwidth.
	frameCounter++;
	if (frameCounter < sendEveryNthFrame) {
		return;
	}
	frameCounter = 0;

	sendImage(ObjectStreamProtocol::previewAddress, mask, frame);

	// Raw metric depth is mapped to a stable 0..4.5 m greyscale. Pi B draws
	// the result only; thresholding and every other CV decision remain on Pi A.
	cv::Mat depthFloat(static_cast<int>(depth.getHeight()),
		static_cast<int>(depth.getWidth()), CV_32FC1,
		const_cast<float *>(depth.getData()));
	depthFloat.convertTo(depth8, CV_8UC1, 255.0 / 4500.0);
	sendImage(CalibrationProtocol::depthPreviewAddress, depth8, frame);
	sendTracks(tracks, frame);
}

void PreviewSender::sendImage(const char * address, const cv::Mat & pixels,
	const ObjectStreamProtocol::FrameIdentity & frame) {
	if (pixels.empty()) {
		return;
	}

	// INTER_AREA rather than nearest-neighbour: it averages the pixels it
	// merges, so a one-pixel-wide blob fades to grey instead of vanishing
	// entirely between two sampled pixels. For a view whose whole purpose is
	// judging whether something is being detected, losing thin structures
	// would defeat the point.
	cv::resize(pixels, scaled, cv::Size(pixels.cols / downscale, pixels.rows / downscale), 0, 0, cv::INTER_AREA);

	int width = scaled.cols;
	int height = scaled.rows;
	if (width <= 0 || height <= 0) {
		return;
	}

	const int stripCount = (height + rowsPerStrip - 1) / rowsPerStrip;
	int stripIndex = 0;
	for (int row = 0; row < height; row += rowsPerStrip, ++stripIndex) {
		int rows = std::min(rowsPerStrip, height - row);

		ofxOscMessage message;
		message.setAddress(address);
		message.addIntArg(width);
		message.addIntArg(height);
		message.addIntArg(row);
		message.addIntArg(rows);

		// cv::resize produces a continuous Mat, so these rows are contiguous
		// in memory and can be handed over as one block.
		ofBuffer strip(reinterpret_cast<const char *>(scaled.ptr<unsigned char>(row)),
			static_cast<size_t>(width) * rows);
		message.addBlobArg(strip);
		message.addInt32Arg(frame.streamId);
		message.addInt32Arg(frame.sequence);
		message.addInt32Arg(stripIndex);
		message.addInt32Arg(stripCount);

		// Sent unbundled: each strip stands on its own, and bundling would
		// only add framing around packets that are already independent.
		sender.sendMessage(message, false);
	}
}

void PreviewSender::sendTracks(
	const std::vector<CalibrationProtocol::TrackDiagnostic> & tracks,
	const ObjectStreamProtocol::FrameIdentity & frame) {
	const int total = std::min<int>(static_cast<int>(tracks.size()),
		CalibrationProtocol::maximumTrackCount);
	const int partCount = std::max(1, (total
		+ CalibrationProtocol::maximumTracksPerDatagram - 1)
		/ CalibrationProtocol::maximumTracksPerDatagram);

	for (int partIndex = 0; partIndex < partCount; ++partIndex) {
		const int first = partIndex * CalibrationProtocol::maximumTracksPerDatagram;
		const int count = std::min(CalibrationProtocol::maximumTracksPerDatagram,
			total - first);

		ofxOscBundle bundle;
		ofxOscMessage header;
		header.setAddress(CalibrationProtocol::trackFrameAddress);
		header.addInt32Arg(frame.streamId);
		header.addInt32Arg(frame.sequence);
		header.addInt32Arg(total);
		header.addInt32Arg(partIndex);
		header.addInt32Arg(partCount);
		header.addInt32Arg(first);
		header.addInt32Arg(count);
		bundle.addMessage(header);

		for (int index = first; index < first + count; ++index) {
			const CalibrationProtocol::TrackDiagnostic & track = tracks[static_cast<size_t>(index)];
			ofxOscMessage message;
			message.setAddress(CalibrationProtocol::trackAddress);
			message.addInt32Arg(frame.streamId);
			message.addInt32Arg(frame.sequence);
			message.addInt32Arg(index);
			message.addInt32Arg(track.id);
			message.addInt32Arg(track.type);
			message.addFloatArg(track.rawX);
			message.addFloatArg(track.rawY);
			message.addFloatArg(track.smoothedX);
			message.addFloatArg(track.smoothedY);
			message.addFloatArg(track.vx);
			message.addFloatArg(track.vy);
			message.addFloatArg(track.palmRadiusMM);
			message.addFloatArg(track.solidity);
			message.addFloatArg(track.borderContact);
			message.addFloatArg(track.stillForSeconds);
			message.addInt32Arg(track.hidden ? 1 : 0);
			bundle.addMessage(message);
		}

		sender.sendBundle(bundle);
	}
}
