#!/bin/bash
# Generate fuzz seed corpus for libqbz2 fuzz harnesses.
# Requires: bzip2 command, dd, python3 (optional for advanced seeds).
# Creates three seed categories:
#   compress_seeds/  — diverse uncompressed inputs (for compression fuzzing)
#   decompress_seeds/ — valid bz2 streams (for decompression fuzzing)
#   malformed_seeds/ — truncated/bit-flipped/invalid bz2 streams (for error-handling fuzzing)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORPUS_DIR="$SCRIPT_DIR/corpus"
COMPRESS_DIR="$CORPUS_DIR/compress_seeds"
DECOMPRESS_DIR="$CORPUS_DIR/decompress_seeds"
MALFORMED_DIR="$CORPUS_DIR/malformed_seeds"
REF_DIR="$SCRIPT_DIR/../reference/bzip2"
REF_QUICK="$REF_DIR/tests/input/quick"

mkdir -p "$COMPRESS_DIR" "$DECOMPRESS_DIR" "$MALFORMED_DIR"

echo "=== Generating compression seeds (uncompressed inputs) ==="

# Empty file
: > "$COMPRESS_DIR/empty"

# Single bytes
printf '\x00' > "$COMPRESS_DIR/single_null"
printf '\xff' > "$COMPRESS_DIR/single_ff"
printf 'A' > "$COMPRESS_DIR/single_A"

# Small strings
printf 'Hello, World!' > "$COMPRESS_DIR/hello"
printf 'aaaa' > "$COMPRESS_DIR/repeat4"
printf 'abcabc' > "$COMPRESS_DIR/pattern_abc"

# Repetitive data (various sizes)
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 256)" > "$COMPRESS_DIR/repeat_A_256"
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 4096)" > "$COMPRESS_DIR/repeat_A_4k"
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 65536)" > "$COMPRESS_DIR/repeat_A_64k"
python3 -c "import sys; sys.stdout.buffer.write(b'\x00' * 65536)" > "$COMPRESS_DIR/zeros_64k"
python3 -c "import sys; sys.stdout.buffer.write(b'\xff' * 65536)" > "$COMPRESS_DIR/ones_64k"

# Sequential bytes
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)))" > "$COMPRESS_DIR/sequential_256"
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) * 40)" > "$COMPRESS_DIR/sequential_10k"

# Random data
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(256))" > "$COMPRESS_DIR/random_256"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(4096))" > "$COMPRESS_DIR/random_4k"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(65536))" > "$COMPRESS_DIR/random_64k"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(1048576))" > "$COMPRESS_DIR/random_1M"

# Text-like data (English-ish byte distribution)
python3 -c "
import sys
text = ('The quick brown fox jumps over the lazy dog. ' * 200 +
        'Pack my box with five dozen liquor jugs. ' * 150 +
        'How vexingly quick daft zebras jump! ' * 100)
sys.stdout.buffer.write(text.encode())
" > "$COMPRESS_DIR/english_text"

# Binary-like structured data
python3 -c "
import sys, struct
data = b''
for i in range(10000):
    data += struct.pack('<IHBd', i, i % 65536, i % 256, float(i) * 3.14)
sys.stdout.buffer.write(data)
" > "$COMPRESS_DIR/structured_binary"

# Run-length patterns (BWT worst/best cases)
python3 -c "
import sys
# Alternating run lengths
data = b''
for length in [1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233]:
    for byte in range(256):
        data += bytes([byte]) * length
        if len(data) > 100000:
            break
    if len(data) > 100000:
        break
sys.stdout.buffer.write(data[:100000])
" > "$COMPRESS_DIR/run_length_patterns"

# Copy reference test input files as compression seeds
if [ -f "$REF_QUICK/sample1.ref" ]; then
    cp "$REF_QUICK/sample1.ref" "$COMPRESS_DIR/ref_sample1"
fi
if [ -f "$REF_QUICK/sample2.ref" ]; then
    cp "$REF_QUICK/sample2.ref" "$COMPRESS_DIR/ref_sample2"
fi
if [ -f "$REF_QUICK/sample3.ref" ]; then
    cp "$REF_QUICK/sample3.ref" "$COMPRESS_DIR/ref_sample3"
fi

# Edge case: data that expands when compressed
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(100))" > "$COMPRESS_DIR/expand_random_100"

# Edge case: exactly block boundary sizes
for bs in 1 2 3 4 5 6 7 8 9; do
    size=$((bs * 100000))
    python3 -c "
import os, sys
sys.stdout.buffer.write(os.urandom($size))
" > "$COMPRESS_DIR/blocksize_${bs}_exact"
done

# Edge case: one byte over/under block boundaries
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(99999))" > "$COMPRESS_DIR/blocksize_1_minus1"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(100001))" > "$COMPRESS_DIR/blocksize_1_plus1"

echo "  Created $(ls "$COMPRESS_DIR" | wc -l) compression seeds"

echo "=== Generating decompression seeds (valid bz2 streams) ==="

# Compress each compression seed at all block sizes
for seed_file in "$COMPRESS_DIR"/*; do
    seed_name="$(basename "$seed_file")"
    # Skip very large files for all block sizes — just use block sizes 1,5,9
    file_size=$(stat -c%s "$seed_file" 2>/dev/null || stat -f%z "$seed_file" 2>/dev/null || echo 0)
    if [ "$file_size" -gt 500000 ]; then
        for bs in 1 5 9; do
            bzip2 -${bs} -c < "$seed_file" > "$DECOMPRESS_DIR/${seed_name}_bs${bs}.bz2" 2>/dev/null || true
        done
    else
        for bs in 1 2 3 4 5 6 7 8 9; do
            bzip2 -${bs} -c < "$seed_file" > "$DECOMPRESS_DIR/${seed_name}_bs${bs}.bz2" 2>/dev/null || true
        done
    fi
done

# Copy reference bz2 test files
if [ -f "$REF_QUICK/sample1.bz2" ]; then
    cp "$REF_QUICK/sample1.bz2" "$DECOMPRESS_DIR/ref_sample1.bz2"
fi
if [ -f "$REF_QUICK/sample2.bz2" ]; then
    cp "$REF_QUICK/sample2.bz2" "$DECOMPRESS_DIR/ref_sample2.bz2"
fi
if [ -f "$REF_QUICK/sample3.bz2" ]; then
    cp "$REF_QUICK/sample3.bz2" "$DECOMPRESS_DIR/ref_sample3.bz2"
fi

echo "  Created $(ls "$DECOMPRESS_DIR" | wc -l) decompression seeds"

echo "=== Generating malformed seeds (invalid/corrupted bz2 streams) ==="

# Take a few valid bz2 files as bases for corruption
BASES=()
for f in "$DECOMPRESS_DIR"/hello_bs5.bz2 "$DECOMPRESS_DIR"/ref_sample1.bz2 "$DECOMPRESS_DIR"/english_text_bs9.bz2; do
    [ -f "$f" ] && BASES+=("$f")
done

if [ ${#BASES[@]} -eq 0 ]; then
    echo "  WARNING: No base files for malformed seeds"
else
    base_idx=0
    for base in "${BASES[@]}"; do
        base_name="base${base_idx}"
        base_size=$(stat -c%s "$base" 2>/dev/null || stat -f%z "$base" 2>/dev/null)

        # Truncations at various offsets
        for offset in 0 1 2 3 4 10 14 "$((base_size / 4))" "$((base_size / 2))" "$((base_size - 1))"; do
            if [ "$offset" -lt "$base_size" ] && [ "$offset" -ge 0 ]; then
                dd if="$base" bs=1 count="$offset" of="$MALFORMED_DIR/${base_name}_trunc_${offset}" 2>/dev/null || true
            fi
        done

        # Bit flips at various positions
        python3 -c "
import sys
data = open('$base', 'rb').read()
for pos in [0, 1, 2, 3, 4, len(data)//4, len(data)//2, len(data)-1]:
    if pos < len(data):
        for bit in [0, 3, 7]:
            corrupted = bytearray(data)
            corrupted[pos] ^= (1 << bit)
            name = '${base_name}_flip_p{}_b{}'.format(pos, bit)
            open('$MALFORMED_DIR/' + name, 'wb').write(corrupted)
" 2>/dev/null || true

        # Byte overwrite with specific values
        python3 -c "
import sys
data = open('$base', 'rb').read()
for pos in [0, 4, 10, len(data)//2]:
    if pos < len(data):
        for val in [0x00, 0xFF, 0x42]:
            corrupted = bytearray(data)
            corrupted[pos] = val
            name = '${base_name}_overwrite_p{}_v{:02x}'.format(pos, val)
            open('$MALFORMED_DIR/' + name, 'wb').write(corrupted)
" 2>/dev/null || true

        base_idx=$((base_idx + 1))
    done

    # Invalid magic bytes
    printf '\x00\x00' > "$MALFORMED_DIR/bad_magic_zeros"
    printf '\x42\x5a\x68\x00' > "$MALFORMED_DIR/bad_magic_blocksize_0"
    printf '\x42\x5a\x68\x3a' > "$MALFORMED_DIR/bad_magic_blocksize_10"
    printf '\x42\x5a\x00\x31' > "$MALFORMED_DIR/bad_magic_h_missing"

    # Valid header but no data
    printf '\x42\x5a\x68\x39' > "$MALFORMED_DIR/header_only_bs9"

    # Valid header + partial block header
    printf '\x42\x5a\x68\x39\x31\x41\x59\x26\x53\x59' > "$MALFORMED_DIR/partial_block_header"

    # Entirely random bytes
    python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(100))" > "$MALFORMED_DIR/random_100"
    python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(1000))" > "$MALFORMED_DIR/random_1000"

    # Concatenated streams (valid bz2 + valid bz2)
    if [ -f "$DECOMPRESS_DIR/hello_bs5.bz2" ] && [ -f "$DECOMPRESS_DIR/ref_sample1.bz2" ]; then
        cat "$DECOMPRESS_DIR/hello_bs5.bz2" "$DECOMPRESS_DIR/ref_sample1.bz2" > "$MALFORMED_DIR/concat_streams"
    fi

    # Valid bz2 followed by garbage
    if [ -f "$DECOMPRESS_DIR/hello_bs5.bz2" ]; then
        cat "$DECOMPRESS_DIR/hello_bs5.bz2" <(python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(50))") > "$MALFORMED_DIR/trailing_garbage"
    fi
fi

echo "  Created $(ls "$MALFORMED_DIR" | wc -l) malformed seeds"

echo "=== Generating multi-block seeds (inputs exceeding block boundaries) ==="

MULTIBLOCK_DIR="$CORPUS_DIR/multiblock_seeds"
mkdir -p "$MULTIBLOCK_DIR"

# Multi-block inputs for block size 1 (100KB boundary)
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 200000)" > "$MULTIBLOCK_DIR/repeat_A_200000"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(100001))" > "$MULTIBLOCK_DIR/random_100001"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(200000))" > "$MULTIBLOCK_DIR/random_200000"
python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom(500000))" > "$MULTIBLOCK_DIR/random_500000"
python3 -c "import sys; sys.stdout.buffer.write(b'\\x00' * 100000)" > "$MULTIBLOCK_DIR/exactly_100k"
python3 -c "import sys; sys.stdout.buffer.write(b'\\x00' * 99999)" > "$MULTIBLOCK_DIR/just_under_100k"

# Text-like multi-block input
python3 -c "
import sys
text = ('The quick brown fox jumps over the lazy dog. ' * 5800)
sys.stdout.buffer.write(text[:260000].encode() if isinstance(text, str) else text[:260000])
" > "$MULTIBLOCK_DIR/text_260000"

echo "  Created $(ls "$MULTIBLOCK_DIR" | wc -l) multi-block seeds"

echo "=== Generating coverage seeds (targeted for code path coverage) ==="

COVERAGE_DIR="$CORPUS_DIR/coverage_seeds"
mkdir -p "$COVERAGE_DIR"

# Large random data (>= 10000 bytes) to exercise mainSort path in blocksort.c
python3 -c "
import random, sys
random.seed(42)
sys.stdout.buffer.write(bytes([random.randint(0,255) for _ in range(20000)]))
" > "$COVERAGE_DIR/large_random_20k.bin"

# Large text-like data
python3 -c "
import random, sys
random.seed(43)
sys.stdout.buffer.write(bytes([random.randint(32,126) for _ in range(15000)]))
" > "$COVERAGE_DIR/large_text_15k.bin"

# Highly repetitive data to trigger SA-IS fallback in blocksort.c
python3 -c "import sys; sys.stdout.buffer.write(b'ABABAB' * 3000)" > "$COVERAGE_DIR/repetitive_ab_18k.bin"
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 20000)" > "$COVERAGE_DIR/repeat_A_20k.bin"
python3 -c "import sys; sys.stdout.buffer.write(b'\\x00\\xff' * 10000)" > "$COVERAGE_DIR/repeat_00ff_20k.bin"

# Ascending/descending patterns
python3 -c "import sys; sys.stdout.buffer.write(bytes([(i % 256) for i in range(20000)]))" > "$COVERAGE_DIR/ascending_20k.bin"
python3 -c "import sys; sys.stdout.buffer.write(bytes([(255 - (i % 256)) for i in range(20000)]))" > "$COVERAGE_DIR/descending_20k.bin"

# RLE-friendly pattern (varied run lengths)
python3 -c "
import random, sys
random.seed(44)
data = b''
for i in range(100):
    ch = bytes([i % 256])
    data += ch * random.randint(1, 255)
sys.stdout.buffer.write(data[:20000])
" > "$COVERAGE_DIR/rle_pattern_20k.bin"

# Small block with limited alphabet (< 10000 for fallbackSort)
python3 -c "
import random, sys
random.seed(45)
sys.stdout.buffer.write(bytes([random.randint(0,3) for _ in range(5000)]))
" > "$COVERAGE_DIR/small_4char_5k.bin"

# Block repeat pattern (long runs of different chars)
python3 -c "import sys; sys.stdout.buffer.write(b'A' * 5000 + b'B' * 5000 + b'A' * 5000 + b'B' * 5000)" > "$COVERAGE_DIR/block_repeat_20k.bin"

echo "  Created $(ls "$COVERAGE_DIR" | wc -l) coverage seeds"

echo ""
echo "=== Corpus summary ==="
echo "  Compression seeds:   $(ls "$COMPRESS_DIR" | wc -l) files"
echo "  Decompression seeds: $(ls "$DECOMPRESS_DIR" | wc -l) files"
echo "  Malformed seeds:     $(ls "$MALFORMED_DIR" | wc -l) files"
echo "  Multi-block seeds:   $(ls "$MULTIBLOCK_DIR" | wc -l) files"
echo "  Coverage seeds:      $(ls "$COVERAGE_DIR" | wc -l) files"
echo "  Total:               $(find "$CORPUS_DIR" -type f | wc -l) files"
echo "  Total size:          $(du -sh "$CORPUS_DIR" | cut -f1)"
echo ""
echo "Corpus generated at: $CORPUS_DIR"
