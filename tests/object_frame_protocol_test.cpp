#include "ObjectFrameAssembler.h"
#include <cassert>
#include <iostream>

using namespace ObjectStreamProtocol;

namespace {
FrameHeader header(int32_t stream, int32_t sequence, int32_t total, int32_t part) {
	FrameHeader value;
	value.identity = FrameIdentity { stream, sequence, static_cast<int64_t>(sequence) * 1000 };
	value.totalObjectCount = total;
	value.partIndex = part;
	value.partCount = partCountForObjects(total);
	value.firstObjectIndex = part * maxObjectsPerDatagram;
	value.partObjectCount = expectedObjectsInPart(total, part);
	return value;
}

FramedObject object(int32_t stream, int32_t sequence, int32_t index) {
	FramedObject value;
	value.streamId = stream;
	value.sequence = sequence;
	value.objectIndex = index;
	value.object.id = index + 100;
	value.object.x = static_cast<float>(index) / 40.0f;
	value.object.y = 0.5f;
	value.object.z = 0.25f;
	value.object.area = 0.01f;
	value.object.type = ObjectType::Hand;
	return value;
}
}

int main() {
	static_assert(maximumDatagramBytes == 1372);
	static_assert(maximumDatagramBytes < ipv4UdpPayloadLimit);

	ObjectFrameAssembler assembler;

	// An explicit empty frame is complete from its header alone.
	auto empty = assembler.addHeader(header(11, 0, 0, 0), 0.0);
	assert(empty.has_value());
	assert(empty->objects.empty());

	// Eighteen objects need two parts. Parts may arrive out of order, but the
	// snapshot is published only after every header and object is present.
	auto secondHeader = assembler.addHeader(header(11, 1, 18, 1), 0.01);
	assert(!secondHeader.has_value());
	assert(!assembler.addObject(object(11, 1, 17), 0.01).has_value());
	assert(!assembler.addHeader(header(11, 1, 18, 0), 0.02).has_value());
	std::optional<ObjectFrameAssembler::CompleteFrame> complete;
	for (int32_t index = 0; index < 17; ++index) {
		complete = assembler.addObject(object(11, 1, index), 0.02);
	}
	assert(complete.has_value());
	assert(complete->objects.size() == 18);
	assert(assembler.getLastCommittedSequence() == 1);

	// A duplicate or older sequence cannot replace the committed snapshot.
	assert(!assembler.addHeader(header(11, 1, 0, 0), 0.03).has_value());
	assert(!assembler.addHeader(header(11, 0, 0, 0), 0.03).has_value());

	// Incomplete multipart input expires without becoming visible.
	assert(!assembler.addHeader(header(11, 2, 18, 0), 0.04).has_value());
	assembler.expire(0.20);
	assert(assembler.getCounters().incompleteFrames == 1);

	// A new stream id represents a sender restart and may begin at sequence 0.
	auto restarted = assembler.addHeader(header(12, 0, 0, 0), 0.21);
	assert(restarted.has_value());
	assert(assembler.getActiveStreamId() == 12);
	assert(assembler.getStreamGeneration() == 2);
	assert(!assembler.addHeader(header(11, 3, 0, 0), 0.22).has_value());

	// A sequence gap is observable without turning it into an empty snapshot.
	auto afterGap = assembler.addHeader(header(12, 2, 0, 0), 0.23);
	assert(afterGap.has_value());
	assert(assembler.getCounters().missingSequences == 1);

	// Committing a newer snapshot also accounts for an older partial snapshot
	// immediately; it need not survive until the timeout to become observable.
	assert(!assembler.addHeader(header(12, 3, 18, 0), 0.24).has_value());
	auto superseding = assembler.addHeader(header(12, 4, 0, 0), 0.25);
	assert(superseding.has_value());
	assert(assembler.getCounters().missingSequences == 2);
	assert(assembler.getCounters().incompleteFrames == 2);

	// Duplicate ids make a snapshot ambiguous and invalidate the whole frame.
	ObjectFrameAssembler duplicateIds;
	assert(!duplicateIds.addHeader(header(21, 0, 2, 0), 0.0).has_value());
	FramedObject first = object(21, 0, 0);
	FramedObject second = object(21, 0, 1);
	second.object.id = first.object.id;
	assert(!duplicateIds.addObject(first, 0.0).has_value());
	assert(!duplicateIds.addObject(second, 0.0).has_value());
	assert(duplicateIds.getCounters().malformedMessages == 1);

	// The non-negative sequence space wraps explicitly without signed overflow.
	ObjectFrameAssembler wrapping;
	assert(wrapping.addHeader(header(31, maximumSequence, 0, 0), 0.0).has_value());
	assert(wrapping.addHeader(header(31, 0, 0, 0), 0.01).has_value());
	assert(wrapping.getCounters().missingSequences == 0);

	FrameHeader tooLarge = header(41, 0, maxObjectsPerFrame, 0);
	tooLarge.totalObjectCount = maxObjectsPerFrame + 1;
	assert(!isValidHeader(tooLarge));

	std::cout << "object frame protocol tests passed\n";
	return 0;
}
