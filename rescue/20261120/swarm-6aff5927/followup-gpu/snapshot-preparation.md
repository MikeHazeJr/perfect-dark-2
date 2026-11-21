# Swarm GPU snapshot preparation receipt

Workbench T-BENCHMARKING-009, owning chat 6aff5927-5abc-8191-8f0b-5cc654a1002f. The only prepared repository path is port/fast3d/swarm_gpu.cpp. This is a separate source-only follow-up after the frozen N-1055 baseline. No canonical source, index, baseline file, native queue, or build was changed.

| Input or output | SHA-256 |
| --- | --- |
| Canonical source at 92200bbdca4127a3d6ab864f197738180ed71819 | eb868029d94a45e65095b2c7af917a9e85f3bfc31bbfdb4862a0b12ea774260c |
| Frozen N-1055 baseline patch | 3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43 |
| Reconstructed one-file baseline preimage | 7ebc741e1102d82cad9e4a1d0e85b222a00b14ce0d02e175342fba5052213d6b |
| Draft C++ source | dabdaf7f035874a1502967a0d3b1477020fa45e38c1b67fb1c291ce3d7b941da |
| One-file follow-up patch | 378d9e429d101c1f2ca96617e3a6f1d3d78fa9bf873bbebe6194e305fda1adab |
| Structural source controls | f9277e2ad23b4ab91b301fd6af2f8ea6ed6368979ebd056bdaf2835738804abd |

The shader uses read-only input at binding 0, read-only parameters at binding 1, the existing write-only texture image at unit 2, and a distinct write-only output buffer at binding 3. Each invocation initializes a local copy from its own input row, reads peer position and velocity from input, sets the serial including zero during regular dispatch, writes the complete 96-byte result, and mirrors that result into the same five texture rows. The engine prop application, actor-lifetime policy, and texture callers remain outside this slice.

The GL command stream moves the existing storage/image barrier to before dispatch so the next shader writes follow earlier consumers of the reused buffer or image. It adds a post-dispatch buffer-update barrier before ring copy or blocking readback, and adds the texture-update bit when image stores are enabled. Those bits cover the commands in the current consumer paths. No texture sampler exists in this path. Upload, async copy, and sync readback share a byte count calculated after count clamping. Fenced reads use the count recorded for the completed ring slot. The ring's order, fallback and wire payload shape are preserved. The output buffer adds 384 KiB at the 4096-row limit. [OpenGL 4.3, section 7.12.2](https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf) · [GLSL 4.30, section 4.10](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.30.pdf)

After the existing test-runner queue admits the offline checks, run python -B test_snapshot_source_contract.py from this directory. The checks validate patch identity, source ownership, GL consumer selection, byte-count and ring provenance, texture source compatibility, and malformed structural variants. They do not exercise or emulate native shader execution.

For integration, first qualify the immutable N-1055 baseline without the follow-up. Refresh canonical dev, reconcile the exact owner and compare the target file to the reconstructed preimage before applying this one-file patch in the shared Git turn. Verify a queued client build. On the same native hardware and inputs, compare fixed supplied snapshots across repeated dispatches and confirm correct ring and blocking output, shader serial zero in the ordinary path, and actual byte visibility in the texture if armed. Test coverage must distinguish driver-local repeatability from cross-hardware floating-point variation. Repeat timing and retain logs before accepting the extra buffer/barrier cost. The current ring can still drop late snapshots, and this patch does not fix generation/occupancy, velocity feedback, collision or encounter behavior.
