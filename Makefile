# Root Makefile for CsLearnHub
# Provides unified test and build targets for automated agents and TDD loops.

LAB_LOCKFREE := 05-hands-on-labs/05-lockfree-pipeline
LAB_MEMORDER := 05-hands-on-labs/memory-order-examples

.PHONY: all test check clean help $(LAB_LOCKFREE) $(LAB_MEMORDER)

all:
	@echo "=== Building 05-lockfree-pipeline ==="
	$(MAKE) -C $(LAB_LOCKFREE) all
	@echo "=== Building memory-order-examples ==="
	$(MAKE) -C $(LAB_MEMORDER) all

# Primary fast verification loop used by TDD and CI
test check:
	@echo "=== Running Lock-Free Pipeline ThreadSanitizer Verification ==="
	$(MAKE) -C $(LAB_LOCKFREE) tsan
	@echo "=== Running Lock-Free Pipeline Standard Run ==="
	$(MAKE) -C $(LAB_LOCKFREE) run
	@echo "=== All Core Tests Passed Successfully ==="

clean:
	$(MAKE) -C $(LAB_LOCKFREE) clean
	$(MAKE) -C $(LAB_MEMORDER) clean
	@echo "[✓] Cleaned all build artifacts."

help:
	@echo "CsLearnHub Top-Level Build & Test Targets:"
	@echo "  make all    - Compile all lab executables"
	@echo "  make test   - Run ThreadSanitizer data race detection & pipeline tests"
	@echo "  make clean  - Clean up all compiled artifacts across labs"

