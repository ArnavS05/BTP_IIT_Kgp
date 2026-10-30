#!/usr/bin/env bash

set -eu

# Default values.
DEFAULT_ALIGNMENT=8
DEFAULT_SCALAR_REGISTERS=16
DEFAULT_VECTOR_REGISTERS=16
DEFAULT_VECTOR_WIDTH=256

show_usage() {
    echo "Usage: bash scripts/run_pipeline.sh <input.tc> [options]"
    echo "  --test"
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
TEST_MODE=false

while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--test" ]]; then
        TEST_MODE=true
        shift
    elif [[ "$1" == "--help" ]]; then
        show_usage
        exit 0
    elif [[ "$1" == "--alignment" || "$1" == "--scalar-registers" ||
            "$1" == "--vector-registers" || "$1" == "--vector-width" ]]; then
        [[ $# -ge 2 ]] || { echo "Error: missing value for $1"; exit 1; }
        case "$1" in
            --alignment) ALIGNMENT="$2" ;;
            --scalar-registers) SCALAR_REGISTERS="$2" ;;
            --vector-registers) VECTOR_REGISTERS="$2" ;;
            --vector-width) VECTOR_WIDTH="$2" ;;
        esac
        shift 2
    else
        echo "Error: unknown option $1"
        exit 1
    fi
done

[[ -f "$INPUT_FILE" ]] || { echo "Error: input file not found"; exit 1; }
[[ "$ALIGNMENT" == 4 || "$ALIGNMENT" == 8 ]] || { echo "Error: alignment must be 4 or 8"; exit 1; }
if ! is_number "$SCALAR_REGISTERS" || [[ ${#SCALAR_REGISTERS} -gt 10 ]] ||
   (( 10#$SCALAR_REGISTERS < 2 || 10#$SCALAR_REGISTERS > 2147483647 )); then
    echo "Error: invalid scalar register count"
    exit 1
fi
if ! is_number "$VECTOR_REGISTERS" || [[ ${#VECTOR_REGISTERS} -gt 10 ]] ||
   (( 10#$VECTOR_REGISTERS < 2 || 10#$VECTOR_REGISTERS > 2147483647 )); then
    echo "Error: invalid vector register count"
    exit 1
fi
[[ "$VECTOR_WIDTH" == 128 || "$VECTOR_WIDTH" == 256 || "$VECTOR_WIDTH" == 512 ]] ||
    { echo "Error: vector width must be 128, 256 or 512"; exit 1; }
SCALAR_REGISTERS=$((10#$SCALAR_REGISTERS))
VECTOR_REGISTERS=$((10#$VECTOR_REGISTERS))

SCRIPT_DIRECTORY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_HOME="$(cd "$SCRIPT_DIRECTORY/.." && pwd)"
SOURCE_DIRECTORY="$(cd "$(dirname "$INPUT_FILE")" && pwd)"
SOURCE_NAME="$(basename "$INPUT_FILE")"
PROGRAM_NAME="${SOURCE_NAME%.*}"
[[ -n "$PROGRAM_NAME" ]] || PROGRAM_NAME="$SOURCE_NAME"
OUTPUT_DIRECTORY="$(realpath --relative-to="$PROJECT_HOME" "$SOURCE_DIRECTORY")"

for PROGRAM in basic_compiler/tinyc simd_optimizer/simd_optimizer \
    reg_allocator/reg_allocator processor_simulator/processor_simulator
do
    [[ -x "$PROJECT_HOME/$PROGRAM" ]] || { echo "Error: project is not built. Run make first."; exit 1; }
done

cd "$PROJECT_HOME"
echo
echo "--------------------------------------------------------"
echo "Pipeline configuration"
echo "Input: $OUTPUT_DIRECTORY/$SOURCE_NAME"
echo "Alignment: $ALIGNMENT bytes"
echo "Scalar registers: $SCALAR_REGISTERS"
echo "Vector registers: $VECTOR_REGISTERS"
echo "Vector width: $VECTOR_WIDTH bits"
echo "Compare scalar and SIMD: $TEST_MODE"

# Compile beside the source, then move its two files out of output_<name>.
echo
echo "[1/4] Compiling TinyC"
(
    cd "$SOURCE_DIRECTORY"
    "$PROJECT_HOME/basic_compiler/tinyc" "$SOURCE_NAME" "$ALIGNMENT"
)
COMPILER_OUTPUT="$OUTPUT_DIRECTORY/output_$PROGRAM_NAME"
[[ -f "$COMPILER_OUTPUT/info.txt" && -f "$COMPILER_OUTPUT/scalar_tac.txt" ]] ||
    { echo "Error: compiler output was not created"; exit 1; }
mv -- "$COMPILER_OUTPUT/info.txt" "$OUTPUT_DIRECTORY/info.txt"
mv -- "$COMPILER_OUTPUT/scalar_tac.txt" "$OUTPUT_DIRECTORY/scalar_tac.txt"
rmdir -- "$COMPILER_OUTPUT" 2>/dev/null || true

echo
echo "[2/4] Running SIMD optimizer"
./simd_optimizer/simd_optimizer "$OUTPUT_DIRECTORY/scalar_tac.txt" "$VECTOR_WIDTH"
[[ -f "$OUTPUT_DIRECTORY/simd_tac.txt" ]] || { echo "Error: SIMD TAC was not created"; exit 1; }

echo
echo "[3/4] Running register allocator"
if [[ "$TEST_MODE" == true ]]; then
    ./reg_allocator/reg_allocator "$OUTPUT_DIRECTORY/scalar_tac.txt" \
        "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
    mv -- "$OUTPUT_DIRECTORY/allocated_tac.txt" "$OUTPUT_DIRECTORY/allocated_scalar_tac.txt"
fi
./reg_allocator/reg_allocator "$OUTPUT_DIRECTORY/simd_tac.txt" \
    "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
if [[ "$TEST_MODE" == true ]]; then
    mv -- "$OUTPUT_DIRECTORY/allocated_tac.txt" "$OUTPUT_DIRECTORY/allocated_simd_tac.txt"
fi

echo
echo "[4/4] Running processor simulator"
if [[ "$TEST_MODE" == true ]]; then
    ./processor_simulator/processor_simulator "$OUTPUT_DIRECTORY/allocated_scalar_tac.txt" \
        "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
    mv -- "$OUTPUT_DIRECTORY/simulation_report.txt" "$OUTPUT_DIRECTORY/scalar_simulation_report.txt"
    ./processor_simulator/processor_simulator "$OUTPUT_DIRECTORY/allocated_simd_tac.txt" \
        "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
    mv -- "$OUTPUT_DIRECTORY/simulation_report.txt" "$OUTPUT_DIRECTORY/simd_simulation_report.txt"

    SCALAR_CYCLES=$(awk '/^Total cycles:/ {print $3; exit}' "$OUTPUT_DIRECTORY/scalar_simulation_report.txt")
    SIMD_CYCLES=$(awk '/^Total cycles:/ {print $3; exit}' "$OUTPUT_DIRECTORY/simd_simulation_report.txt")
    is_number "$SCALAR_CYCLES" && is_number "$SIMD_CYCLES" ||
        { echo "Error: missing cycle totals"; exit 1; }
    awk -v scalar="$SCALAR_CYCLES" -v simd="$SIMD_CYCLES" 'BEGIN {
        if(scalar<=0 || simd<=0) exit 1;
        printf "Scalar cycles: %.0f\nSIMD cycles: %.0f\n",scalar,simd;
        printf "Measured SIMD speedup: %.4fx\n",scalar/simd;
        printf "Cycle reduction: %.2f%%\n",100*(scalar-simd)/scalar;
    }' > "$OUTPUT_DIRECTORY/comparison.txt"
    echo
    cat "$OUTPUT_DIRECTORY/comparison.txt"
else
    ./processor_simulator/processor_simulator "$OUTPUT_DIRECTORY/allocated_tac.txt" \
        "$SCALAR_REGISTERS" "$VECTOR_REGISTERS" "$VECTOR_WIDTH"
fi

echo
echo "Pipeline complete"
echo "Output directory: $OUTPUT_DIRECTORY"
