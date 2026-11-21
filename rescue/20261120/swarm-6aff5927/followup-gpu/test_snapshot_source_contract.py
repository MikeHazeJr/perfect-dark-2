"""Structural checks on the off-checkout Swarm snapshot draft.

These pin source contracts and patch provenance. They do not execute OpenGL or
prove native compute, synchronization, actor lifetime, or game behavior.
"""
from pathlib import Path
import re
import unittest

from prepare_immutable_snapshot import apply_frozen_file, digest

DIRECTORY = Path(__file__).resolve().parent
BASELINE = DIRECTORY / "swarm_gpu.frozen-baseline.cpp"
DRAFT = DIRECTORY / "swarm_gpu.immutable-snapshot.cpp"
DELTA = DIRECTORY / "swarm-gpu-immutable-snapshot-after-n1055.patch"
BASELINE_SHA = "7ebc741e1102d82cad9e4a1d0e85b222a00b14ce0d02e175342fba5052213d6b"


def compact(text):
    without_comments = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
    return re.sub(r"\s+", "", without_comments)


def missing_contracts(source):
    shader = source.split('R"GLSL(', 1)[1].split(')GLSL"', 1)[0]
    shader_body = shader.split("void main() {", 1)[1]
    shader_flat = compact(shader)
    body_flat = compact(shader_body)
    host = source.split("void swarmGpuStepAndApply(", 1)[1]
    host_flat = compact(host)

    ownership = (
        "layout(std430,binding=0)readonlybufferBoidInput{Boidinput_boids[];};" in shader_flat
        and "layout(std430,binding=1)readonlybufferParams{" in shader_flat
        and "layout(std430,binding=3)writeonlybufferBoidOutput{Boidoutput_boids[];};" in shader_flat
        and "layout(rgba32f,binding=2)uniformwriteonlyimage2DStateOut;" in shader_flat
        and "Boidresult=input_boids[i];" in body_flat
        and "vec3p_j=vec3(input_boids[j].px,input_boids[j].py,input_boids[j].pz);" in body_flat
        and "align_sum+=vec3(input_boids[j].vx,input_boids[j].vy,input_boids[j].vz);" in body_flat
        and body_flat.count("output_boids[") == 1
        and "output_boids[i]=result;" in body_flat
        and not re.search(r"input_boids\[\w+\](?:\.\w+)?[+\-*/]?=(?!=)", body_flat)
    )

    storage = (
        "static_assert(sizeof(boid_record)==96," in compact(source)
        and "glGenBuffers(2,buffers);" in compact(source)
        and "s_BoidInputSsbo=buffers[0];s_BoidOutputSsbo=buffers[1];" in compact(source)
        and "s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,s_BoidInputSsbo);" in host_flat
        and "s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,s_ParamsSsbo);" in host_flat
        and "s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,s_BoidOutputSsbo);" in host_flat
        and "constGLsizeiptrsubmitted_bytes=sizeof(boid_record)*count;" in host_flat
        and "glBindBuffer(GL_SHADER_STORAGE_BUFFER,s_BoidInputSsbo);"
        "glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,submitted_bytes,s_BoidScratch);" in host_flat
    )

    synchronization = (
        "s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|"
        "(state_tex_write?GL_SHADER_IMAGE_ACCESS_BARRIER_BIT:0));"
        "s_glDispatchCompute(groups,1,1);"
        "s_glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|"
        "(state_tex_write?GL_TEXTURE_UPDATE_BARRIER_BIT:0));" in host_flat
    )

    readback = (
        "glBindBuffer(GL_COPY_READ_BUFFER,s_BoidOutputSsbo);"
        "glBindBuffer(GL_COPY_WRITE_BUFFER,s_ReadbackRingBuf[write_idx]);"
        "s_glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,submitted_bytes);" in host_flat
        and "s_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);"
        "s_ReadbackRingCount[write_idx]=count;"
        "s_BaselineRing[write_idx]=submitted;" in host_flat
        and "if(wait==GL_ALREADY_SIGNALED||wait==GL_CONDITION_SATISFIED){"
        "consumed_count=s_ReadbackRingCount[read_idx];"
        "readback=s_BaselineRing[read_idx];" in host_flat
        and "glGetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(boid_record)*consumed_count,s_BoidScratch);" in host_flat
        and "glBindBuffer(GL_SHADER_STORAGE_BUFFER,s_BoidOutputSsbo);"
        "glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,submitted_bytes,s_BoidScratch);" in host_flat
    )

    marker = (
        re.search(r"(?m)^    result\._pad_m0 = P\._pad_s0;\s*output_boids\[i\] = result;", shader_body)
        and "swarm_baseline_submissionsubmitted={};" in host_flat
        and "s_Params._pad_s0=submitted.serial;" in host_flat
    )

    return {
        name for name, passed in (
            ("shader_ownership", ownership),
            ("host_storage", storage),
            ("consumer_barriers", synchronization),
            ("readback_delivery", readback),
            ("diagnostic_marker", marker),
        ) if not passed
    }


class SnapshotSourceChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = BASELINE.read_bytes()
        cls.draft = DRAFT.read_text(encoding="utf-8")

    def test_patch_reconstructs_draft_from_frozen_preimage(self):
        self.assertEqual(digest(self.baseline), BASELINE_SHA)
        patch = DELTA.read_text(encoding="utf-8")
        patch_paths = [row for row in patch.splitlines() if row.startswith("diff --git ")]
        self.assertEqual(patch_paths, ["diff --git a/port/fast3d/swarm_gpu.cpp b/port/fast3d/swarm_gpu.cpp"])
        self.assertEqual(apply_frozen_file(self.baseline.decode("utf-8"), patch), self.draft)

    def test_new_source_meets_all_structural_contracts(self):
        self.assertEqual(missing_contracts(self.draft), set())

    def test_original_source_has_no_snapshot_separation(self):
        self.assertIn("shader_ownership", missing_contracts(self.baseline.decode("utf-8")))

    def test_texture_receive_and_transmit_functions_match_frozen_source(self):
        begin = "int swarmGpuReadbackTextureRows("
        end = "/* B-308 first slice (c3807, 2026-05-15) tuning constants."
        def texture_io(source):
            return source.split(begin, 1)[1].split(end, 1)[0]
        self.assertEqual(texture_io(self.draft), texture_io(self.baseline.decode("utf-8")))

    def test_reject_shader_writes_to_peer_input(self):
        mutated = self.draft.replace("result.px = pos.x;", "input_boids[i].px = pos.x;")
        self.assertIn("shader_ownership", missing_contracts(mutated))

    def test_reject_host_binding_alias(self):
        mutated = self.draft.replace("3, s_BoidOutputSsbo", "3, s_BoidInputSsbo")
        self.assertIn("host_storage", missing_contracts(mutated))

    def test_reject_buffer_barrier_only_in_diagnostics(self):
        mutated = self.draft.replace(
            "s_glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT",
            "s_glMemoryBarrier((submitted.serial ? GL_BUFFER_UPDATE_BARRIER_BIT : 0)",
        )
        self.assertIn("consumer_barriers", missing_contracts(mutated))

    def test_reject_texture_barrier_for_wrong_consumer(self):
        mutated = self.draft.replace(
            "(state_tex_write ? GL_TEXTURE_UPDATE_BARRIER_BIT : 0)",
            "(state_tex_write ? GL_SHADER_IMAGE_ACCESS_BARRIER_BIT : 0)",
        )
        self.assertIn("consumer_barriers", missing_contracts(mutated))

    def test_reject_ring_copy_from_input(self):
        mutated = self.draft.replace(
            "glBindBuffer(GL_COPY_READ_BUFFER, s_BoidOutputSsbo)",
            "glBindBuffer(GL_COPY_READ_BUFFER, s_BoidInputSsbo)",
        )
        self.assertIn("readback_delivery", missing_contracts(mutated))

    def test_reject_transfer_with_wrong_batch_size(self):
        mutated = self.draft.replace(
            "0, 0, submitted_bytes);", "0, 0, sizeof(boid_record) * SWARM_GPU_MAX);"
        )
        self.assertIn("readback_delivery", missing_contracts(mutated))

    def test_reject_marker_that_can_survive_normal_dispatch(self):
        mutated = self.draft.replace(
            "result._pad_m0 = P._pad_s0;",
            "if (P._pad_s0 != 0) result._pad_m0 = P._pad_s0;",
        )
        self.assertIn("diagnostic_marker", missing_contracts(mutated))


if __name__ == "__main__":
    unittest.main(verbosity=2)
