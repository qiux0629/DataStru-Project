from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
import heapq
import struct

MAGIC = b"HUF1"


@dataclass(order=True)
class _Node:
    freq: int
    order: int
    symbol: int | None = field(compare=False, default=None)
    left: "_Node | None" = field(compare=False, default=None)
    right: "_Node | None" = field(compare=False, default=None)


@dataclass
class CompressionStats:
    input_path: Path
    output_path: Path
    original_size: int
    compressed_size: int

    @property
    def compression_ratio(self) -> float:
        if self.original_size == 0:
            return 0.0
        return self.compressed_size / self.original_size

    @property
    def saving_ratio(self) -> float:
        if self.original_size == 0:
            return 0.0
        return 1.0 - self.compression_ratio


@dataclass
class DecompressionStats:
    input_path: Path
    output_path: Path
    compressed_size: int
    restored_size: int


class _BitWriter:
    def __init__(self) -> None:
        self._buffer = bytearray()
        self._current = 0
        self._fill = 0
        self.total_bits = 0

    def write(self, code: int, bit_length: int) -> None:
        for i in range(bit_length - 1, -1, -1):
            bit = (code >> i) & 1
            self._current = (self._current << 1) | bit
            self._fill += 1
            self.total_bits += 1
            if self._fill == 8:
                self._buffer.append(self._current)
                self._current = 0
                self._fill = 0

    def finish(self) -> bytes:
        if self._fill > 0:
            self._current <<= 8 - self._fill
            self._buffer.append(self._current)
            self._current = 0
            self._fill = 0
        return bytes(self._buffer)


class _BitReader:
    def __init__(self, data: bytes, total_bits: int) -> None:
        self._data = data
        self._total_bits = total_bits
        self._consumed = 0

    def read(self) -> int | None:
        if self._consumed >= self._total_bits:
            return None
        byte_index = self._consumed // 8
        bit_offset = 7 - (self._consumed % 8)
        self._consumed += 1
        return (self._data[byte_index] >> bit_offset) & 1


def _build_tree(freq_map: dict[int, int]) -> _Node | None:
    if not freq_map:
        return None

    heap: list[_Node] = []
    order = 0
    for symbol, freq in sorted(freq_map.items()):
        heap.append(_Node(freq=freq, order=order, symbol=symbol))
        order += 1

    heapq.heapify(heap)
    while len(heap) > 1:
        left = heapq.heappop(heap)
        right = heapq.heappop(heap)
        parent = _Node(freq=left.freq + right.freq, order=order, left=left, right=right)
        order += 1
        heapq.heappush(heap, parent)
    return heap[0]


def _collect_code_lengths(node: _Node | None, depth: int, out: dict[int, int]) -> None:
    if node is None:
        return
    if node.symbol is not None:
        out[node.symbol] = max(1, depth)
        return
    _collect_code_lengths(node.left, depth + 1, out)
    _collect_code_lengths(node.right, depth + 1, out)


def _build_canonical_codes(code_lengths: dict[int, int]) -> dict[int, tuple[int, int]]:
    canonical: dict[int, tuple[int, int]] = {}
    current_code = 0
    previous_length = 0

    for symbol, length in sorted(code_lengths.items(), key=lambda item: (item[1], item[0])):
        if length <= 0:
            raise ValueError("Invalid Huffman code length.")
        if length > 255:
            raise ValueError("Huffman code length too large to serialize.")
        current_code <<= length - previous_length
        canonical[symbol] = (current_code, length)
        current_code += 1
        previous_length = length
    return canonical


def compress_bytes(data: bytes) -> bytes:
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")
    raw = bytes(data)

    header = bytearray(MAGIC)
    if not raw:
        header.extend(struct.pack(">QH", 0, 0))
        header.extend(struct.pack(">Q", 0))
        return bytes(header)

    freq_map = Counter(raw)
    tree = _build_tree(freq_map)
    code_lengths: dict[int, int] = {}
    _collect_code_lengths(tree, 0, code_lengths)
    codes = _build_canonical_codes(code_lengths)

    writer = _BitWriter()
    for value in raw:
        code, bit_length = codes[value]
        writer.write(code, bit_length)
    payload = writer.finish()

    header.extend(struct.pack(">QH", len(raw), len(code_lengths)))
    for symbol, length in sorted(code_lengths.items()):
        header.extend(struct.pack("BB", symbol, length))
    header.extend(struct.pack(">Q", writer.total_bits))
    return bytes(header) + payload


def decompress_bytes(blob: bytes) -> bytes:
    data = memoryview(blob)
    if len(data) < 4 + 10 + 8:
        raise ValueError("Invalid compressed file: too short.")

    cursor = 0
    if data[:4].tobytes() != MAGIC:
        raise ValueError("Invalid compressed file: bad magic.")
    cursor += 4

    original_size, symbol_count = struct.unpack_from(">QH", data, cursor)
    cursor += 10

    code_lengths: dict[int, int] = {}
    for _ in range(symbol_count):
        if cursor + 2 > len(data):
            raise ValueError("Invalid compressed file: truncated symbol table.")
        symbol, length = struct.unpack_from("BB", data, cursor)
        cursor += 2
        if length == 0:
            raise ValueError("Invalid compressed file: zero-length code.")
        code_lengths[symbol] = length

    if cursor + 8 > len(data):
        raise ValueError("Invalid compressed file: missing bit-length field.")
    payload_bits, = struct.unpack_from(">Q", data, cursor)
    cursor += 8

    expected_payload_bytes = (payload_bits + 7) // 8
    payload = data[cursor:].tobytes()
    if len(payload) < expected_payload_bytes:
        raise ValueError("Invalid compressed file: payload truncated.")
    if len(payload) > expected_payload_bytes:
        payload = payload[:expected_payload_bytes]

    if original_size == 0:
        if symbol_count != 0 or payload_bits != 0:
            raise ValueError("Invalid compressed file: inconsistent empty header.")
        return b""

    if not code_lengths:
        raise ValueError("Invalid compressed file: no symbols for non-empty payload.")

    codes = _build_canonical_codes(code_lengths)
    decode_map = {(length, code): symbol for symbol, (code, length) in codes.items()}
    max_length = max(code_lengths.values())

    reader = _BitReader(payload, payload_bits)
    result = bytearray()
    while len(result) < original_size:
        current_code = 0
        matched = False
        for bit_length in range(1, max_length + 1):
            bit = reader.read()
            if bit is None:
                raise ValueError("Invalid compressed file: unexpected end of bitstream.")
            current_code = (current_code << 1) | bit
            symbol = decode_map.get((bit_length, current_code))
            if symbol is not None:
                result.append(symbol)
                matched = True
                break
        if not matched:
            raise ValueError("Invalid compressed file: undecodable stream.")
    return bytes(result)


def compress_file(input_path: str | Path, output_path: str | Path | None = None) -> CompressionStats:
    in_path = Path(input_path)
    if not in_path.exists():
        raise FileNotFoundError(f"Input file not found: {in_path}")

    out_path = Path(output_path) if output_path else Path(f"{in_path}.huf")
    raw = in_path.read_bytes()
    compressed = compress_bytes(raw)
    out_path.write_bytes(compressed)
    return CompressionStats(
        input_path=in_path,
        output_path=out_path,
        original_size=len(raw),
        compressed_size=len(compressed),
    )


def decompress_file(
    input_path: str | Path, output_path: str | Path | None = None
) -> DecompressionStats:
    in_path = Path(input_path)
    if not in_path.exists():
        raise FileNotFoundError(f"Input file not found: {in_path}")

    if output_path is None:
        if in_path.suffix.lower() == ".huf":
            candidate = in_path.with_suffix("")
            out_path = (
                candidate
                if candidate.suffix
                else candidate.with_name(f"{candidate.name}_decoded.txt")
            )
        else:
            out_path = Path(f"{in_path}.decoded.txt")
    else:
        out_path = Path(output_path)

    compressed = in_path.read_bytes()
    restored = decompress_bytes(compressed)
    out_path.write_bytes(restored)
    return DecompressionStats(
        input_path=in_path,
        output_path=out_path,
        compressed_size=len(compressed),
        restored_size=len(restored),
    )


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Huffman compressor/decompressor")
    subparsers = parser.add_subparsers(dest="command", required=True)

    compress_parser = subparsers.add_parser("compress", help="Compress a text file")
    compress_parser.add_argument("input", type=str, help="Input text file path")
    compress_parser.add_argument("-o", "--output", type=str, default=None, help="Output .huf file")

    decompress_parser = subparsers.add_parser("decompress", help="Decompress a .huf file")
    decompress_parser.add_argument("input", type=str, help="Input .huf file path")
    decompress_parser.add_argument("-o", "--output", type=str, default=None, help="Output text file")

    args = parser.parse_args()
    if args.command == "compress":
        stats = compress_file(args.input, args.output)
        print(f"Compressed: {stats.input_path} -> {stats.output_path}")
        print(f"Original: {stats.original_size} bytes")
        print(f"Compressed: {stats.compressed_size} bytes")
        print(f"Compression ratio: {stats.compression_ratio * 100:.2f}%")
    else:
        stats = decompress_file(args.input, args.output)
        print(f"Decompressed: {stats.input_path} -> {stats.output_path}")
        print(f"Compressed size: {stats.compressed_size} bytes")
        print(f"Restored size: {stats.restored_size} bytes")
