# Talmud

A self-documenting agent knowledge system. Drop it into any C project and your AI agents get a searchable, tree-structured knowledge base that compiles into a single binary.

Replaces `.md` file sprawl, "skills", MCP servers, and every other LLM-era abstraction with something that actually works: pure C, `gcc` only, zero dependencies.

## What It Does

Agents read documentation by calling CLI commands, not by ingesting thousands of tokens of markdown. Every node is capped at 4095 bytes — the constraint forces clarity and forbids bloat. The tree structure is the overflow mechanism.

**Tools:**
- **talmud** — Knowledge tree CLI. Ranked search, mandala navigator, purgatory task queue.
- **yotzer** — Build system. Compiles all targets, self-reexecs when stale. No make, no cmake.
- **darshan** — Code analysis. Function dependency graphs, reference search, smart replace.
- **sofer** — Node scribe. Programmatic add, purge, count, list of knowledge nodes.

## Getting Started

```bash
# 1. Copy the Talmud folder into your project root
cp -r /path/to/Talmud /path/to/YourProject/Talmud

# 2. Run the bris (one-time configuration ceremony)
cd YourProject/Talmud
./install.sh

# 3. Enter your project's purpose when prompted
#    e.g. "BUILD A CUSTOM 3D MODELER"

# 4. The first agent to run `talmud` will see the bris node
#    and begin documenting your codebase
```

The bris asks for your project's purpose, updates the configuration, builds all tools, and creates induction nodes that guide the first agent through full codebase documentation. Once documentation is complete, the bris node self-destructs.

## Requirements

- `gcc` (C11, `-Wall -Wextra -Werror -pedantic`)
- That's it.

## History

Born from [Durgios](https://github.com/), a custom version control system. The documentation layer outgrew its host and was extracted as a standalone tool. The religious naming convention carries forward — every name is chosen from the tradition whose concept best fits the tool (see `talmud reference doctrine naming`).

## License

See LICENSE file.
