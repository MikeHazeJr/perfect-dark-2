"""Prepare the Swarm GPU snapshot patch from the frozen baseline, off-checkout."""
from pathlib import Path
import argparse
import difflib
import hashlib
import re


REPO_PATH = "port/fast3d/swarm_gpu.cpp"
FROZEN_SHA = "3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43"
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def apply_frozen_file(preimage, patch):
    source = preimage.splitlines(keepends=True)
    changes = patch.splitlines(keepends=True)
    start = changes.index(f"diff --git a/{REPO_PATH} b/{REPO_PATH}\n")
    stop = next(
        (i for i in range(start + 1, len(changes)) if changes[i].startswith("diff --git ")),
        len(changes),
    )
    output = []
    cursor = 0
    i = start + 3
    while i < stop:
        match = HUNK.match(changes[i])
        if not match:
            raise ValueError(f"Unexpected patch header at line {i + 1}")
        old_start, old_count, new_start, new_count = (
            int(value) if value is not None else 1 for value in match.groups()
        )
        index = old_start - 1
        if index < cursor:
            raise ValueError("Overlapping frozen hunks")
        output.extend(source[cursor:index])
        if len(output) != new_start - 1:
            raise ValueError("Frozen hunk output offset mismatch")
        cursor = index
        i += 1
        new_lines = 0
        while i < stop and not changes[i].startswith("@@ "):
            line = changes[i]
            prefix, contents = line[0], line[1:]
            if prefix in " -":
                if cursor >= len(source) or source[cursor] != contents:
                    raise ValueError(f"Frozen baseline context mismatch at source line {cursor + 1}")
                cursor += 1
            if prefix in " +":
                output.append(contents)
                new_lines += 1
            if prefix not in " +-":
                raise ValueError(f"Unexpected patch line {i + 1}")
            i += 1
        if cursor - index != old_count or new_lines != new_count:
            raise ValueError("Frozen hunk count mismatch")
    output.extend(source[cursor:])
    return "".join(output)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected exactly one source block starting {old[:80]!r}")
    return text.replace(old, new, 1)


def immutable_snapshot(baseline):
    draft = replace_once(
        baseline,
        """ *  - A single SSBO holds the boid state. After B-308 first slice
 *    (c3807, 2026-05-15) the per-boid record is 96 bytes -- pos + vel +""",
        """ *  - The host uploads an immutable input SSBO. Each invocation writes
 *    its own record into a separate output SSBO. A record is 96 bytes
 *    and contains position, velocity, and""",
    )
    draft = replace_once(
        draft,
        """ *    surface_up (the legacy 48-byte block) plus 48 bytes of GPU-AI""",
        """ *    surface_up (the first 48 bytes) plus 48 bytes of GPU-AI""",
    )
    draft = replace_once(
        draft,
        """ * over all peers in the same SSBO (O(N^2) inner loop) and blended with""",
        """ * over all peers in the input SSBO (O(N^2) inner loop) and blended with""",
    )
    draft = replace_once(
        draft,
        """#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif""",
        """#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_TEXTURE_UPDATE_BARRIER_BIT
#define GL_TEXTURE_UPDATE_BARRIER_BIT     0x00000100
#endif""",
    )
    draft = replace_once(
        draft,
        """struct swarm_params {""",
        """static_assert(sizeof(boid_record) == 96, "Boid must match the std430 shader record");

struct swarm_params {""",
    )
    draft = replace_once(
        draft,
        """ * from the same SSBO. Two vec4-packed groups so std140/std430 padding""",
        """ * from the immutable input SSBO. Two vec4-packed groups so std140/std430 padding""",
    )
    draft = replace_once(
        draft,
        """static GLuint        s_BoidSsbo     = 0;""",
        """static GLuint        s_BoidInputSsbo  = 0;
static GLuint        s_BoidOutputSsbo = 0;""",
    )
    draft = replace_once(
        draft,
        """ * Compute writes BOTH the SSBO (existing CPU readback path) AND the
 * texture every frame; bit-exact behaviour parity with pre-2a is
 * preserved because the SSBO is the only thing the CPU side consumes.""",
        """ * Compute mirrors each output record into the texture for extraction
 * and network sync. Actor application uses the output SSBO readback.""",
    )
    draft = replace_once(
        draft,
        """ * sample it via glGetTexImage; the kernel keeps reading state from the
 * SSBO.""",
        """ * sample it via glGetTexImage; the kernel keeps reading state from the
 * input SSBO.""",
    )
    draft = replace_once(
        draft,
        """ *   1. Dispatch compute (writes boid SSBO)
 *   2. glCopyBufferSubData(BoidSsbo -> ring[write_idx])""",
        """ *   1. Dispatch compute (writes the boid output SSBO)
 *   2. glCopyBufferSubData(output SSBO -> ring[write_idx])""",
    )
    draft = replace_once(
        draft,
        """layout(std430, binding = 0) buffer Boids {
    Boid b[];
};

layout(std430, binding = 1) buffer Params {""",
        """layout(std430, binding = 0) readonly buffer BoidInput {
    Boid input_boids[];
};

layout(std430, binding = 1) readonly buffer Params {""",
    )
    draft = replace_once(
        draft,
        """} P;

/* Track 2a (c3807, 2026-05-16): ping-pong state texture (write side).""",
        """} P;

layout(std430, binding = 3) writeonly buffer BoidOutput {
    Boid output_boids[];
};

/* Track 2a (c3807, 2026-05-16): ping-pong state texture (write side).""",
    )
    draft = replace_once(
        draft,
        """ * writeonly so the driver can skip read-tracking; image binding 2 is
 * free (0 and 1 are the boid + params SSBOs already bound above).""",
        """ * Image unit 2 remains reserved for the texture mirror.""",
    )
    draft = replace_once(
        draft,
        """    vec3 pos = vec3(b[i].px, b[i].py, b[i].pz);""",
        """    Boid result = input_boids[i];
    vec3 pos = vec3(result.px, result.py, result.pz);""",
    )
    draft = replace_once(
        draft,
        """        /* ---- Track 4 v0 (c3807, 2026-05-16) classical Reynolds boids ----
         * Three-rule accumulator over peers in the SSBO. Naive O(N^2):
         * every invocation reads every other invocation's pos+vel from
         * the SAME boid buffer this dispatch is writing to. std430
         * defines this as a write-after-read race for a given index j,
         * but the only writes per invocation are to b[i] (px/py/pz/vx/vy/vz
         * + AI block). Peers' fields read here (px/py/pz/vx/vy/vz) are
         * the START-of-frame snapshot the CPU just uploaded; the
         * compute kernel doesn't reissue between reads for different i,
         * so we get pre-step positions for everyone. Equivalent to a
         * double-buffer in effect at this dispatch frequency.
         *
         * Spatial-grid v1 (k-nearest broad phase) is the next slice; v0
         * just demonstrates that the steering math is wired and bots
         * separate / align / cohere visually.
         */""",
        """        /* Reynolds steering reads the same pre-step positions and
         * velocities for every invocation, independent of shader scheduling. */""",
    )
    draft = replace_once(
        draft,
        """     * Mirror the SSBO record into a 5-texel column in StateOut. The
     * texture writes are AFTER all SSBO writes so the texture and SSBO
     * agree on per-bot state at end-of-dispatch. Reading b[i] back here
     * is safe because std430 ordering within a single invocation is
     * sequential (no cross-invocation race on a given index).
""",
        """     * Mirror the same invocation-local result into the 5-texel column.
""",
    )
    draft = replace_once(
        draft,
        """    if (P._pad_s0 != 0) {
        b[i]._pad_m0 = P._pad_s0;
    }
    if (P.state_tex_enable != 0) {""",
        """    result._pad_m0 = P._pad_s0;
    output_boids[i] = result;
    if (P.state_tex_enable != 0) {""",
    )
    shader_start = draft.index("void main() {")
    shader_end = draft.index(')GLSL";', shader_start)
    shader = draft[shader_start:shader_end].replace("b[i].", "result.").replace("b[j].", "input_boids[j].")
    draft = draft[:shader_start] + shader + draft[shader_end:]

    draft = replace_once(
        draft,
        """	if (s_BoidSsbo == 0) {
		glGenBuffers(1, &s_BoidSsbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
		glBufferData(GL_SHADER_STORAGE_BUFFER,
			sizeof(boid_record) * SWARM_GPU_MAX, NULL, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}""",
        """	if (s_BoidInputSsbo == 0) {
		GLuint buffers[2] = {};
		glGenBuffers(2, buffers);
		if (buffers[0] == 0 || buffers[1] == 0) {
			glDeleteBuffers(2, buffers);
			return 0;
		}

		constexpr GLint capacity = sizeof(boid_record) * SWARM_GPU_MAX;
		for (GLuint buffer : buffers) {
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, capacity, NULL, GL_DYNAMIC_DRAW);
			GLint allocated = 0;
			glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &allocated);
			if (allocated != capacity) {
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				glDeleteBuffers(2, buffers);
				return 0;
			}
		}
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		s_BoidInputSsbo = buffers[0];
		s_BoidOutputSsbo = buffers[1];
	}""",
    )
    draft = replace_once(
        draft,
        """	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
		sizeof(boid_record) * count, s_BoidScratch);""",
        """	const GLsizeiptr submitted_bytes = sizeof(boid_record) * count;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidInputSsbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, submitted_bytes, s_BoidScratch);""",
    )
    draft = replace_once(
        draft,
        """	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_BoidSsbo);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_ParamsSsbo);""",
        """	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_BoidInputSsbo);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_ParamsSsbo);
	s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, s_BoidOutputSsbo);""",
    )
    draft = replace_once(
        draft,
        """	s_glDispatchCompute(groups, 1, 1);
	/* Track 2a (c3807, 2026-05-16): expand the post-dispatch barrier to
	 * cover image stores as well as SSBO writes. The barrier ensures
	 * subsequent CPU readback (glGetBufferSubData) and any image-sampling
	 * consumer (none today; slated for 2b) see the dispatch's writes.
	 * Bit cost is negligible vs the SSBO bit alone. */
	s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
		| (submitted.serial ? GL_BUFFER_UPDATE_BARRIER_BIT : 0));""",
        """	/* Order shader writes after consumers of earlier output, including
	 * the previous ring copy before reusing the output SSBO. */
	s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
		| (state_tex_write ? GL_SHADER_IMAGE_ACCESS_BARRIER_BIT : 0));
	s_glDispatchCompute(groups, 1, 1);
	/* The ring copy and blocking read use buffer commands. The texture
	 * mirror is read by glGetTexImage. Both need their own visibility bit. */
	s_glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT
		| (state_tex_write ? GL_TEXTURE_UPDATE_BARRIER_BIT : 0));""",
    )
    draft = replace_once(
        draft,
        """	 *   2. Copy this frame's BoidSsbo -> ring[write_idx] via""",
        """	 *   2. Copy this frame's output SSBO -> ring[write_idx] via""",
    )
    draft = replace_once(
        draft,
        """		glBindBuffer(GL_COPY_READ_BUFFER, s_BoidSsbo);
		glBindBuffer(GL_COPY_WRITE_BUFFER, s_ReadbackRingBuf[write_idx]);
		s_glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
			0, 0, (GLsizeiptr)(sizeof(boid_record) * count));""",
        """		glBindBuffer(GL_COPY_READ_BUFFER, s_BoidOutputSsbo);
		glBindBuffer(GL_COPY_WRITE_BUFFER, s_ReadbackRingBuf[write_idx]);
		s_glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
			0, 0, submitted_bytes);""",
    )
    draft = replace_once(
        draft,
        """		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidSsbo);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
			sizeof(boid_record) * count, s_BoidScratch);""",
        """		glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_BoidOutputSsbo);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, submitted_bytes, s_BoidScratch);""",
    )
    return draft


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("canonical_source", type=Path)
    parser.add_argument("frozen_patch", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    patch = args.frozen_patch.read_bytes()
    if digest(patch) != FROZEN_SHA:
        raise ValueError("Frozen baseline patch hash mismatch")
    canonical = args.canonical_source.read_bytes()
    baseline = apply_frozen_file(canonical.decode("utf-8"), patch.decode("utf-8"))
    draft = immutable_snapshot(baseline)
    delta = f"diff --git a/{REPO_PATH} b/{REPO_PATH}\n" + "".join(
        difflib.unified_diff(
            baseline.splitlines(keepends=True),
            draft.splitlines(keepends=True),
            f"a/{REPO_PATH}",
            f"b/{REPO_PATH}",
        )
    )
    for name, data in (
        ("swarm_gpu.frozen-baseline.cpp", baseline),
        ("swarm_gpu.immutable-snapshot.cpp", draft),
        ("swarm-gpu-immutable-snapshot-after-n1055.patch", delta),
    ):
        data = data.encode("utf-8")
        with (args.output / name).open("xb") as stream:
            stream.write(data)
        print(digest(data), name)
    print(digest(canonical), "canonical source")


if __name__ == "__main__":
    main()
