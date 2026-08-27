# TinyC AVX Compiler

This project compiles a restricted TinyC program into scalar three-address code, passes it through the SIMD optimizer, and performs register allocation. The processor simulator is currently a placeholder and does not produce timing results yet.

## Build

The project requires GCC/G++, Flex, Bison, Make and Bash.

```bash
make
```

## Run the complete pipeline

Run using the default configuration:

```bash
./scripts/run_pipeline.sh input.tc
```

The defaults are 8-byte alignment, 16 scalar registers, 16 vector registers and a 256-bit vector width. They are written near the top of `scripts/run_pipeline.sh` and can be edited directly.

Values can also be overridden for one run:

```bash
./scripts/run_pipeline.sh input.tc \
    --alignment 4 \
    --scalar-registers 8 \
    --vector-registers 8 \
    --vector-width 512
```

The Makefile provides a shorter default-only command:

```bash
make run INPUT=input.tc
```

## Input and output

The input must be a valid TinyC source file. For `input.tc`, the pipeline creates:

```text
output_input/
├── info.txt
├── scalar_tac.txt
├── simd_tac.txt
└── allocated_tac.txt
```

`info.txt` contains type and symbol-table information. The three TAC files contain the output after scalar compilation, SIMD optimization and register allocation respectively.

Run the included checks using:

```bash
make test
```
