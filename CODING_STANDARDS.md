# CsLearnHub Coding Standards & Review Guidelines

This document specifies the authoritative coding and engineering standards for all code and technical documentation in this repository. All code implementations, pull requests, and automated reviews (`/code-review`) must strictly verify compliance against these standards.

---

## 1. Code Design & Quality Standards

### 1.1 English-Only for Code Comments
- **Strict English**: All in-code comments (`//`, `/* ... */`), function documentation, module headers, and docstrings across all languages (C/C++, Python, Rust, Go, Shell) **must be written strictly in professional English**.
- No Chinese characters or mixed-language phrases are permitted inside code files or code blocks.

### 1.2 No Narrative Comments (Explain the Why, Not the What)
- **Eliminate Clutter**: Never repeat what the syntax already says (e.g., do not add `// allocate memory` before `malloc`, or `// queue is full` before `return false;`).
- **Explain Design Rationale**: Focus comments on *why* this design choice was made, why naive alternatives failed, and how hardware realities (cache lines, pipeline stalls, branch prediction) influenced the decision.

### 1.3 Invariants and Memory Order Pairs
- **Header Invariants**: Every concurrent data structure and state machine must declare its core abstract invariants at the top of its declaration.
- **Happens-Before Pairs**: Every atomic operation using memory ordering weaker than `seq_cst` (e.g., `acquire`, `release`, `relaxed`) must explicitly document its corresponding synchronization partner (the *Happens-Before Pair*).
- **Ownership & Mutex-Free Boundaries**: Clearly document Single-Producer-Single-Consumer (SPSC) or Multiple-Producer (MPMC) access boundaries.

### 1.4 Sanity as Assertions
- Enforce size, offset, and alignment invariants at compile time using `static_assert` (or `_Static_assert` in C11).
- Enforce power-of-two constraints, buffer boundaries, and pointer validity at runtime using deterministic assertions (`assert`). Never leave core safety guarantees solely to comments.

### 1.5 Deep Modules & Small Interfaces
- Follow the "small interface, deep implementation" principle (*A Philosophy of Software Design*).
- Encapsulate low-level synchronization details (QSBR state machines, memory fences, ring buffer wrapping) behind simple, clean lifecycle functions.

---

## 2. Low-Level & High-Performance Engineering

### 2.1 Cache-Line & Hardware Awareness
- Data structures subject to concurrent thread access must align critical write-heavy fields to 64-byte boundaries (`alignas(64)`) to prevent **False Sharing**.
- Hot fields and cold management metadata must be placed on distinct cache lines.

### 2.2 Concurrency & Memory Safety
- **No Data Races**: Code must pass ThreadSanitizer (`-fsanitize=thread`) with zero warnings.
- **Reclamation & Safety**: Non-blocking data structures must eliminate Use-After-Free (UAF) and ABA hazards using provable techniques (e.g., QSBR / Epoch-based reclamation, generation counters, or Hazard Pointers).

---

## 3. Documentation & Visual Alignment Standards

### 3.1 Strict ASCII / Box-Drawing Alignment
- When drawing ASCII memory layouts, ring buffers, or pipeline flowcharts:
  - Standard ASCII and box characters (`─`, `│`, `┌`, `┐`, `└`, `┘`, `├`, `┤`, `┼`) have visual width **1**.
  - CJK fullwidth characters have visual width **2**.
  - Right borders must close perfectly with zero jagged edges or column drift.

### 3.2 Terminological Purity
- Standard systems and concurrency terminology (e.g., *Cache Line*, *False Sharing*, *Fast Path*, *QSBR*, *Drain Barrier*, *Happens-Before*, *Memory Order*, *Epoch*) must be used directly in pure English. Do not append redundant bracketed Chinese translations.

### 3.3 Mermaid Visual Aesthetics
- Mermaid diagrams must include the unified directive init (`Modern Slate` theme) with `#64748b` lines and `#e0f2fe` primary highlights. Avoid high-saturation solid color fills.

