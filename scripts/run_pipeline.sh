#!/usr/bin/env bash

set -e

# Default values. Edit these four lines if you want different defaults.
DEFAULT_ALIGNMENT=8
DEFAULT_SCALAR_REGISTERS=16
DEFAULT_VECTOR_REGISTERS=16
DEFAULT_VECTOR_WIDTH=256

show_usage() {
    echo "Usage: ./scripts/run_pipeline.sh <input.tc> [options]"
    echo
    echo "Options:"
    echo "  --alignment <4|8>"
    echo "  --scalar-registers <number (>=2)>"
    echo "  --vector-registers <number (>=2)>"
    echo "  --vector-width <128|256|512>"
    echo "  --help"
}

is_number() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

if [[ $# -eq 0 ]]; then
    show_usage
    exit 1
fi

if [[ "$1" == "--help" ]]; then
    show_usage
    exit 0
fi

INPUT_FILE="$1"
shift

ALIGNMENT=$DEFAULT_ALIGNMENT
SCALAR_REGISTERS=$DEFAULT_SCALAR_REGISTERS
VECTOR_REGISTERS=$DEFAULT_VECTOR_REGISTERS
VECTOR_WIDTH=$DEFAULT_VECTOR_WIDTH

while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--alignment" ]]; then
        [[ $# -ge 2 ]] || { echo "Error: missing alignment value"; exit 1; }
        ALIGNMENT="$2"
        shift 2
    elif [[ "$1" == "--scalar-registers" ]]; then
        [[ $# -ge 2 ]] || { echo "Error: missing scalar-register count"; exit 1; }
        SCALAR_REGISTERS="$2"
        shift 2
    elif [[ "$1" == "--vector-registers" ]]; then
        [[ $# -ge 2 ]] || { echo "Error: missing vector-register count"; exit 1; }
        VECTOR_REGISTERS="$2"
        shift 2
    elif [[ "$1" == "--vector-width" ]]; then
        [[ $# -ge 2 ]] || { echo "Error: missing vector width"; exit 1; }
        VECTOR_WIDTH="$2"
        shift 2
    else
        echo "Error: unknown option $1"
        show_usage
        exit 1
    fi
done

if [[ ! -f "$INPUT_FILE" ]]; then
    echo "Error: input file not found: $INPUT_FILE"
    exit 1
fi

if [[ "$ALIGNMENT" != "4" && "$ALIGNMENT" != "8" ]]; then
    echo "Error: alignment must be 4 or 8"
    exit 1
fi

if ! is_number "$SCALAR_REGISTERS" || (( SCALAR_REGISTERS < 2 )); then
    echo "Error: at least two scalar registers are required"
    exit 1
fi

if ! is_number "$VECTOR_REGISTERS" || (( VECTOR_REGISTERS < 2 )); then
    echo "Error: at least two vector registers are required"
    exit 1
fi

if [[ "$VECTOR_WIDTH" != "128" && "$VECTOR_WIDTH" != "256" && "$VECTOR_WIDTH" != "512" ]]; then
    echo "Error: vector width must be 128, 256 or 512"
    exit 1
fi

SCRIPT_DIRECTORY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_HOME="$(cd "$SCRIPT_DIRECTORY/.." && pwd)"

if [[ "$INPUT_FILE" != /* ]]; then
    INPUT_FILE="$(pwd)/$INPUT_FILE"
fi

SOURCE_NAME="$(basename "$INPUT_FILE")"
PROGRAM_NAME="$SOURCE_NAME"
if [[ "$SOURCE_NAME" == *.* ]]; then
    NAME_WITHOUT_EXTENSION="${SOURCE_NAME%.*}"
    if [[ -n "$NAME_WITHOUT_EXTENSION" ]]; then
        PROGRAM_NAME="$NAME_WITHOUT_EXTENSION"
    fi
fi
OUTPUT_DIRECTORY="output_$PROGRAM_NAME"

for PROGRAM in \
    "$PROJECT_HOME/basic_compiler/tinyc" \
    "$PROJECT_HOME/simd_optimizer/simd_optimizer" \
    "$PROJECT_HOME/reg_allocator/reg_allocator" \
    "$PROJECT_HOME/processor_simulator/processor_simulator"
do
    if [[ ! -x "$PROGRAM" ]]; then
        echo "Error: project is not built. Run make from Project_Home first."
        exit 1
    fi
done

echo 
echo "--------------------------------------------------------"
echo "Pipeline configuration"
echo "Input: $SOURCE_NAME"
echo "Alignment: $ALIGNMENT bytes"
echo "Scalar registers: $SCALAR_REGISTERS"
echo "Vector registers: $VECTOR_REGISTERS"
echo "Vector width: $VECTOR_WIDTH bits"
echo

cd "$PROJECT_HOME"

echo "[1/4] Compiling TinyC"
./basic_compiler/tinyc "$INPUT_FILE" "$ALIGNMENT"
[[ -f "$OUTPUT_DIRECTORY/scalar_tac.txt" ]] || { echo "Error: scalar TAC was not created"; exit 1; }

echo
echo "[2/4] Running SIMD optimizer"
./simd_optimizer/simd_optimizer "$OUTPUT_DIRECTORY/scalar_tac.txt" "$VECTOR_WIDTH"
[[ -f "$OUTPUT_DIRECTORY/simd_tac.txt" ]] || { echo "Error: SIMD TAC was not created"; exit 1; }

echo
echo "[3/4] Running register allocator"
./reg_allocator/reg_allocator "$OUTPUT_DIRECTORY/simd_tac.txt" \
    "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
[[ -f "$OUTPUT_DIRECTORY/allocated_tac.txt" ]] || { echo "Error: allocated TAC was not created"; exit 1; }

echo
echo "[4/4] Running processor simulator"
./processor_simulator/processor_simulator "$OUTPUT_DIRECTORY/allocated_tac.txt" \
    "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
[[ -f "$OUTPUT_DIRECTORY/simulation_report.txt" ]] || { echo "Error: simulation report was not created"; exit 1; }

echo
echo "Pipeline complete"
echo "Output directory: $OUTPUT_DIRECTORY"
