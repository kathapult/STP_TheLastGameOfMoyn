#!/usr/bin/env python3
"""Build, verify and optionally transmit the OSC v1 installation contract.

Run without arguments for a dependency-free wire-size/shape self-test. Run
with --send from Pi A, while sensorApp is stopped, to exercise Pi B with empty,
multipart, reordered, duplicated and intentionally incomplete object frames.
"""

from __future__ import annotations

import argparse
import random
import socket
import struct
import time
from collections.abc import Iterable, Sequence


OBJECT_PORT = 8000
PREVIEW_PORT = 8001
MAX_OBJECTS_PER_PACKET = 17
MAX_OBJECTS_PER_FRAME = 40
IPV4_UDP_PAYLOAD_LIMIT = 1472


def padded(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) % 4)


def osc_string(value: str) -> bytes:
    return padded(value.encode("ascii") + b"\0")


def osc_message(address: str, type_tags: str, values: Sequence[object]) -> bytes:
    if len(type_tags) != len(values):
        raise ValueError("one OSC type tag is required for every value")

    result = bytearray(osc_string(address))
    result.extend(osc_string("," + type_tags))
    for type_tag, value in zip(type_tags, values, strict=True):
        if type_tag == "i":
            result.extend(struct.pack(">i", int(value)))
        elif type_tag == "h":
            result.extend(struct.pack(">q", int(value)))
        elif type_tag == "f":
            result.extend(struct.pack(">f", float(value)))
        elif type_tag == "b":
            blob = bytes(value)
            result.extend(struct.pack(">i", len(blob)))
            result.extend(padded(blob))
        else:
            raise ValueError(f"unsupported OSC type tag: {type_tag}")
    return bytes(result)


def osc_bundle(messages: Iterable[bytes]) -> bytes:
    # Immediate OSC timetag (1) matches ofxOscBundle's send semantics.
    result = bytearray(b"#bundle\0" + struct.pack(">Q", 1))
    for message in messages:
        result.extend(struct.pack(">i", len(message)))
        result.extend(message)
    return bytes(result)


def object_values(stream_id: int, sequence: int, index: int) -> tuple[object, ...]:
    return (
        1000 + index,
        (index % 40) / 40.0,
        0.5,
        0.25,
        0.02,
        0.0,
        0.0,
        0.1,
        1,
        stream_id,
        sequence,
        index,
    )


def object_frame_packets(
    stream_id: int, sequence: int, object_count: int
) -> list[bytes]:
    if not 0 <= object_count <= MAX_OBJECTS_PER_FRAME:
        raise ValueError("object_count must be between 0 and 40")

    part_count = max(1, (object_count + MAX_OBJECTS_PER_PACKET - 1)
                     // MAX_OBJECTS_PER_PACKET)
    sender_time_us = time.monotonic_ns() // 1000
    packets: list[bytes] = []
    for part_index in range(part_count):
        first_index = part_index * MAX_OBJECTS_PER_PACKET
        part_object_count = min(MAX_OBJECTS_PER_PACKET,
                                object_count - first_index)
        header = osc_message(
            "/frame/v1",
            "iihiiiii",
            (
                stream_id,
                sequence,
                sender_time_us,
                object_count,
                part_index,
                part_count,
                first_index,
                part_object_count,
            ),
        )
        objects = [
            osc_message(
                "/object",
                "ifffffffiiii",
                object_values(stream_id, sequence, index),
            )
            for index in range(first_index, first_index + part_object_count)
        ]
        packets.append(osc_bundle([header, *objects]))
    return packets


def preview_packets(stream_id: int, sequence: int) -> list[bytes]:
    width = 128
    height = 106
    rows_per_strip = 8
    strip_count = (height + rows_per_strip - 1) // rows_per_strip
    packets = []
    for strip_index, row in enumerate(range(0, height, rows_per_strip)):
        rows = min(rows_per_strip, height - row)
        pixels = bytes(((row + offset) % 256 for offset in range(width * rows)))
        packets.append(
            osc_message(
                "/preview",
                "iiiibiiii",
                (width, height, row, rows, pixels,
                 stream_id, sequence, strip_index, strip_count),
            )
        )
    return packets


def bundle_addresses(packet: bytes) -> list[str]:
    if not packet.startswith(b"#bundle\0") or len(packet) < 16:
        raise AssertionError("not an OSC bundle")
    addresses = []
    offset = 16
    while offset < len(packet):
        element_size = struct.unpack_from(">i", packet, offset)[0]
        offset += 4
        if element_size <= 0 or offset + element_size > len(packet):
            raise AssertionError("invalid OSC bundle element size")
        message = packet[offset:offset + element_size]
        terminator = message.find(b"\0")
        if terminator <= 0:
            raise AssertionError("invalid OSC address")
        addresses.append(message[:terminator].decode("ascii"))
        offset += element_size
    if offset != len(packet):
        raise AssertionError("trailing OSC bundle bytes")
    return addresses


def self_test() -> None:
    for object_count, expected_parts in ((0, 1), (1, 1), (17, 1), (18, 2), (40, 3)):
        packets = object_frame_packets(12345, object_count, object_count)
        assert len(packets) == expected_parts
        for part_index, packet in enumerate(packets):
            assert len(packet) <= IPV4_UDP_PAYLOAD_LIMIT
            addresses = bundle_addresses(packet)
            assert addresses[0] == "/frame/v1"
            expected_objects = min(
                MAX_OBJECTS_PER_PACKET,
                max(0, object_count - part_index * MAX_OBJECTS_PER_PACKET),
            )
            assert addresses[1:] == ["/object"] * expected_objects

    largest = object_frame_packets(12345, 1, 17)[0]
    assert len(largest) == 1372
    previews = preview_packets(12345, 1)
    assert len(previews) == 14
    assert max(map(len, previews)) == 1084
    assert max(map(len, previews)) < IPV4_UDP_PAYLOAD_LIMIT
    print("OSC v1 wire self-test passed (max object datagram: 1372 bytes)")


def transmit(host: str, object_port: int, preview_port: int) -> None:
    stream_id = random.SystemRandom().randrange(1, 2**31)
    object_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    preview_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    scenarios = [
        (0, object_frame_packets(stream_id, 0, 0)),
        (1, object_frame_packets(stream_id, 1, 1)),
        (2, list(reversed(object_frame_packets(stream_id, 2, 18)))),
        (3, object_frame_packets(stream_id, 3, 40)),
    ]
    for sequence, packets in scenarios:
        for packet in packets:
            object_socket.sendto(packet, (host, object_port))
        if sequence == 2:
            object_socket.sendto(packets[0], (host, object_port))
        time.sleep(0.25)

    # Sequence 4 is deliberately partial. Sequence 5 must supersede it and
    # publish an explicit empty snapshot rather than any partial object set.
    partial = object_frame_packets(stream_id, 4, 40)
    object_socket.sendto(partial[0], (host, object_port))
    time.sleep(0.05)
    object_socket.sendto(object_frame_packets(stream_id, 5, 0)[0],
                         (host, object_port))

    preview = preview_packets(stream_id, 5)
    for packet in reversed(preview):
        preview_socket.sendto(packet, (host, preview_port))

    print(
        f"sent OSC v1 probe stream {stream_id} to {host}; "
        "verify Pi B HUD counters and the final empty object snapshot"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--send", metavar="HOST",
                        help="transmit the probe after the local self-test")
    parser.add_argument("--port", type=int, default=OBJECT_PORT,
                        help="object-stream UDP port (default: 8000)")
    parser.add_argument("--preview-port", type=int, default=PREVIEW_PORT,
                        help="preview UDP port (default: 8001)")
    args = parser.parse_args()

    self_test()
    if args.send:
        transmit(args.send, args.port, args.preview_port)


if __name__ == "__main__":
    main()
