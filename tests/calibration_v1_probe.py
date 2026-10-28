#!/usr/bin/env python3
"""Verify and optionally probe the OSC calibration v1 wire contract.

The local self-test is dependency-free. With --send it binds UDP 8002 on Pi B,
requests state from Pi A and checks that an unknown field is rejected without
changing the confirmed revision. Stop visualApp first so the port is free.
"""

from __future__ import annotations

import argparse
import random
import socket
import struct
import time
from collections.abc import Iterable, Sequence


CONTROL_PORT = 8002
IPV4_UDP_PAYLOAD_LIMIT = 1472
FIELD_COUNT = 25
TABLE_MASK_AWARE_SETTINGS_COUNT = 23
TABLE_MASK_METADATA_COUNT = 5
INTERACTION_POINT_SETTINGS_COUNT = 2


def padded(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) % 4)


def osc_string(value: str) -> bytes:
    return padded(value.encode("ascii") + b"\0")


def osc_message(address: str, type_tags: str,
                values: Sequence[object]) -> bytes:
    if len(type_tags) != len(values):
        raise ValueError("one OSC type tag is required for every value")
    result = bytearray(osc_string(address))
    result.extend(osc_string("," + type_tags))
    for type_tag, value in zip(type_tags, values, strict=True):
        if type_tag == "i":
            result.extend(struct.pack(">i", int(value)))
        elif type_tag == "f":
            result.extend(struct.pack(">f", float(value)))
        elif type_tag == "s":
            result.extend(osc_string(str(value)))
        elif type_tag == "b":
            blob = bytes(value)
            result.extend(struct.pack(">i", len(blob)))
            result.extend(padded(blob))
        else:
            raise ValueError(f"unsupported OSC type tag: {type_tag}")
    return bytes(result)


def osc_bundle(messages: Iterable[bytes]) -> bytes:
    result = bytearray(b"#bundle\0" + struct.pack(">Q", 1))
    for message in messages:
        result.extend(struct.pack(">i", len(message)))
        result.extend(message)
    return bytes(result)


def settings_values() -> tuple[object, ...]:
    return (
        500.0, 1000.0, 20.0, 200.0, 32.0, 15,
        0.0, 0.0, 1.0, 1.0, 1, 2,
        1.2, 0.4, 0.8, 25.0, 90.0, 5.0, 0.02, 10.0, 0.25, 1, 15.0,
    )


def state_packet(session: int, revision: int, request_id: int) -> bytes:
    tags = ("iiiii" + "fffffi" + "ffff" + "ii" + "fffffffff"
            + "if" + "iiiif" + "if")
    values = (1, session, revision, request_id, 0, *settings_values(),
              1, 2, 0, 15, 0.98, 1, 0.15)
    assert FIELD_COUNT == (TABLE_MASK_AWARE_SETTINGS_COUNT
                           + INTERACTION_POINT_SETTINGS_COUNT)
    assert len(values) == (5 + TABLE_MASK_AWARE_SETTINGS_COUNT
                           + TABLE_MASK_METADATA_COUNT
                           + INTERACTION_POINT_SETTINGS_COUNT)
    return osc_message("/calibration/v1/state", tags, values)


def depth_packets(stream_id: int, sequence: int) -> list[bytes]:
    width, height, rows_per_strip = 128, 106, 8
    strip_count = (height + rows_per_strip - 1) // rows_per_strip
    packets = []
    for strip_index, row in enumerate(range(0, height, rows_per_strip)):
        rows = min(rows_per_strip, height - row)
        pixels = bytes((row + offset) % 256 for offset in range(width * rows))
        packets.append(osc_message(
            "/calibration/depth/v1", "iiiibiiii",
            (width, height, row, rows, pixels, stream_id, sequence,
             strip_index, strip_count),
        ))
    return packets


def track_bundle(stream_id: int, sequence: int, count: int = 10) -> bytes:
    header = osc_message(
        "/calibration/tracks/v1", "iiiiiii",
        (stream_id, sequence, count, 0, 1, 0, count),
    )
    tracks = []
    for index in range(count):
        tracks.append(osc_message(
            "/calibration/track/v1", "iiiiiffffffffffi",
            (stream_id, sequence, index, 100 + index, 2,
             0.2, 0.3, 0.21, 0.31, 0.0, 0.0,
             45.0, 0.7, 0.1, 1.5, 0),
        ))
    return osc_bundle([header, *tracks])


def parse_address(packet: bytes) -> str:
    end = packet.find(b"\0")
    if end <= 0:
        raise AssertionError("invalid OSC address")
    return packet[:end].decode("ascii")


def parse_int_args(packet: bytes) -> tuple[str, list[int]]:
    address = parse_address(packet)
    address_bytes = len(osc_string(address))
    tag_end = packet.find(b"\0", address_bytes)
    tags = packet[address_bytes + 1:tag_end].decode("ascii")
    offset = address_bytes + len(osc_string("," + tags))
    values: list[int] = []
    for tag in tags:
        if tag == "i":
            values.append(struct.unpack_from(">i", packet, offset)[0])
            offset += 4
        elif tag in {"f", "s", "b"}:
            break
    return address, values


def self_test() -> None:
    state = state_packet(123, 7, 9)
    assert parse_address(state) == "/calibration/v1/state"
    assert len(state) < IPV4_UDP_PAYLOAD_LIMIT

    get = osc_message("/calibration/v1/get", "ii", (1, 9))
    set_value = osc_message(
        "/calibration/v1/set", "iiiif", (1, 10, 7, 6, 0.2))
    result = osc_message(
        "/calibration/v1/result", "iiiiis", (1, 123, 10, 8, 0, "ok"))
    capture = osc_message(
        "/calibration/v1/capture-table-mask", "iii", (1, 11, 8))
    clear = osc_message(
        "/calibration/v1/clear-table-mask", "iii", (1, 12, 9))
    assert all(len(packet) < IPV4_UDP_PAYLOAD_LIMIT
               for packet in (get, set_value, result, capture, clear))

    depth = depth_packets(123, 1)
    assert len(depth) == 14
    assert max(map(len, depth)) == 1096
    assert max(map(len, depth)) < IPV4_UDP_PAYLOAD_LIMIT

    tracks = track_bundle(123, 1)
    assert len(tracks) == 1204
    assert len(tracks) < IPV4_UDP_PAYLOAD_LIMIT
    print("calibration v1 wire self-test passed "
          "(max depth: 1096, max tracks: 1204 bytes)")


def receive_until(sock: socket.socket, wanted: set[str], timeout: float) -> dict[str, bytes]:
    deadline = time.monotonic() + timeout
    received: dict[str, bytes] = {}
    while time.monotonic() < deadline and wanted - received.keys():
        sock.settimeout(max(0.05, deadline - time.monotonic()))
        try:
            packet, _ = sock.recvfrom(4096)
        except TimeoutError:
            break
        address = parse_address(packet)
        if address in wanted:
            received[address] = packet
    return received


def probe(host: str, port: int) -> None:
    request_id = random.SystemRandom().randrange(1, 2**31 - 2)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("0.0.0.0", port))
        sock.sendto(osc_message(
            "/calibration/v1/get", "ii", (1, request_id)), (host, port))
        replies = receive_until(
            sock,
            {"/calibration/v1/state", "/calibration/v1/result"},
            2.0,
        )
        if "/calibration/v1/state" not in replies:
            raise RuntimeError("Pi A returned no calibration state")

        _, metadata = parse_int_args(replies["/calibration/v1/state"])
        if len(metadata) < 5:
            raise RuntimeError("Pi A returned malformed state metadata")
        revision = metadata[2]

        # Unknown field 999 must not mutate state. The server responds with an
        # explicit error plus its unchanged complete state.
        request_id += 1
        sock.sendto(osc_message(
            "/calibration/v1/set", "iiiif",
            (1, request_id, revision, 999, 1.0)), (host, port))
        rejected = receive_until(
            sock,
            {"/calibration/v1/state", "/calibration/v1/result"},
            2.0,
        )
        if len(rejected) != 2:
            raise RuntimeError("Pi A did not return complete rejection response")
        _, result_values = parse_int_args(rejected["/calibration/v1/result"])
        _, rejected_state = parse_int_args(rejected["/calibration/v1/state"])
        if result_values[4] != 3 or rejected_state[2] != revision:
            raise RuntimeError("unknown field was not rejected without revision change")
    print(f"calibration control probe passed against {host}:{port}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--send", metavar="HOST",
                        help="probe Pi A after the local self-test")
    parser.add_argument("--port", type=int, default=CONTROL_PORT,
                        help="local/remote control port (default: 8002)")
    args = parser.parse_args()
    self_test()
    if args.send:
        probe(args.send, args.port)


if __name__ == "__main__":
    main()
