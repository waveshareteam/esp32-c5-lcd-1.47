# Repository Structure

| Path | Maintained content |
| --- | --- |
| `assets/` | Product images used by the README |
| `examples/esp-idf/` | Eight direct ESP-IDF projects |
| `examples/arduino/` | Eight direct Arduino sketches |
| `libraries/` | Repository-pinned Arduino libraries and their upstream files |
| `config/` | CI pins, board options, and factory-image metadata |
| `scripts/` | Example discovery and workflow contract tests |
| `releases/` | Firmware packaging, validation, and artifact download tools |
| `firmware/` | Checked-in factory recovery image |
| `hardware/schematics/` | Product schematic PDF |
| `hardware/dimensions/` | 2D, 3D, and drawing references |
| `docs/` | Maintainer and user documentation |
| `.github/` | CI workflows and collaboration templates |

Only direct projects under `examples/esp-idf/` and direct sketches under
`examples/arduino/` are product CI inputs. Nested CMake projects and sketches in
vendored dependencies are not independently built.

`build/`, `.ci-build/`, `managed_components/`, dependency locks, packaged ZIP
archives, and downloaded workflow artifacts are generated locally or by CI and
must remain untracked. Files under `firmware/` are checked-in recovery inputs,
not outputs of the source-build workflow.
