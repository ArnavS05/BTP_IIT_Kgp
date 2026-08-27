.PHONY: all basic_compiler tac architecture simd_optimizer \
	reg_allocator processor_simulator test run clean

all: basic_compiler tac architecture simd_optimizer \
	reg_allocator processor_simulator

basic_compiler:
	$(MAKE) -C basic_compiler

tac:
	$(MAKE) -C tac

architecture:
	$(MAKE) -C architecture

simd_optimizer:
	$(MAKE) -C simd_optimizer

reg_allocator:
	$(MAKE) -C reg_allocator

processor_simulator:
	$(MAKE) -C processor_simulator

run: all
	@if [ -z "$(INPUT)" ]; then \
		echo "Usage: make run INPUT=path/to/input.tc"; \
		exit 1; \
	fi
	bash scripts/run_pipeline.sh "$(INPUT)"

clean:
	$(MAKE) -C basic_compiler clean
	$(MAKE) -C tac clean
	$(MAKE) -C architecture clean
	$(MAKE) -C simd_optimizer clean
	$(MAKE) -C reg_allocator clean
	$(MAKE) -C processor_simulator clean
	rm -rf output_*