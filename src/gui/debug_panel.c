/* Goxel 3D voxels editor
 *
 * copyright (c) 2019 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

void gui_debug_panel(void)
{
    volume_global_stats_t stats;
    render_perf_stats_t perf;
    gpu_mirror_stats_t mirror;
    const gpu_capabilities_t *caps =
        gpu_accel_get_capabilities(goxel.gpu_accel);

    gui_text("FPS: %d", (int)round(goxel.fps));
    volume_get_global_stats(&stats);
    gui_text("Nb volumes: %d", stats.nb_volumes);
    gui_text("Nb tiles: %d", stats.nb_tiles);
    gui_text("Mem: %dM", (int)(stats.mem / (1 << 20)));
    gui_text("GPU mode: %s", gpu_accel_mode_name(goxel.gpu_accel_mode));
    gui_text("GPU backend: %s",
             caps ? gpu_accel_backend_name(caps->backend) : "CPU/OpenGL");
    if (caps && caps->device_name[0])
        gui_text("GPU device: %s", caps->device_name);
    gpu_accel_get_mirror_stats(goxel.gpu_accel, &mirror);
    gui_text("Tile mirror: %llu / %llu source",
             (unsigned long long)mirror.mirrored_tiles,
             (unsigned long long)mirror.source_tiles);
    gui_text("Mirror memory: %.2f / %.2f MB",
             mirror.used_bytes / (1024.0 * 1024.0),
             mirror.capacity_bytes / (1024.0 * 1024.0));
    gui_text("Mirror dirty/halo: %llu / %llu",
             (unsigned long long)mirror.dirty_tiles,
             (unsigned long long)mirror.halo_tiles);
    gui_text("Mirror upload: %llu tiles / %.2f MB",
             (unsigned long long)mirror.uploads,
             mirror.uploaded_bytes / (1024.0 * 1024.0));
    gui_text("Mirror evict/remove/checksum errors: %llu / %llu / %llu",
             (unsigned long long)mirror.evictions,
             (unsigned long long)mirror.removals,
             (unsigned long long)mirror.checksum_mismatches);
    gui_text("Metal mesh: %llu ok / %llu attempts",
             (unsigned long long)mirror.mesh_successes,
             (unsigned long long)mirror.mesh_attempts);
    gui_text("Mesh fallback/mismatch/overflow: %llu / %llu / %llu",
             (unsigned long long)mirror.mesh_fallbacks,
             (unsigned long long)mirror.mesh_validation_mismatches,
             (unsigned long long)mirror.mesh_overflows);
    gui_text("Metal mesh time: %.2f ms", mirror.mesh_gpu_ms);
    gui_text("Metal path preview: %llu frames / %llu attempts",
             (unsigned long long)mirror.path_preview_frames,
             (unsigned long long)mirror.path_preview_attempts);
    gui_text("Path reset/fallback/time: %llu / %llu / %.2f ms",
             (unsigned long long)mirror.path_preview_resets,
             (unsigned long long)mirror.path_preview_fallbacks,
             mirror.path_preview_gpu_ms);
    gui_text("Path preview memory: %.2f MB",
             mirror.path_preview_memory_bytes / (1024.0 * 1024.0));

    render_perf_get_stats(&perf);
    gui_text("Mesh cache: %llu hit / %llu miss",
             (unsigned long long)perf.cache_hits,
             (unsigned long long)perf.cache_misses);
    gui_text("CPU mesh: %.2f ms", perf.cpu_mesh_ms);
    gui_text("Buffer upload: %.2f ms / %.2f MB",
             perf.buffer_upload_ms,
             perf.uploaded_bytes / (1024.0 * 1024.0));
    gui_text("Submit CPU: %.2f ms", perf.submit_cpu_ms);
    gui_text("Picking: %llu cached / %llu renders (%.2f ms)",
             (unsigned long long)perf.pick_cache_hits,
             (unsigned long long)perf.pick_renders,
             perf.pick_render_ms);
    gui_text("Pick readback: %llu / %.2f ms",
             (unsigned long long)perf.pick_readbacks,
             perf.pick_readback_ms);
    gui_text("GPU-assisted tool preview frames: %llu",
             (unsigned long long)perf.tool_preview_frames);

    if (!DEFINED(GLES2)) {
        gui_checkbox_flag("Show wireframe", &goxel.view_effects,
                          EFFECT_WIREFRAME, NULL);
    }

    if (gui_button("Clear undo history", -1, 0)) {
        image_history_resize(goxel.image, 0);
    }
    if (gui_button("On low memory", -1, 0)) {
        goxel_on_low_memory();
    }
    if (gui_button("Test release", -1, 0)) {
        goxel.request_test_graphic_release = true;
    }
    if (gui_button("Log GPU baseline report", -1, 0)) {
        render_perf_log_report();
    }
    if (gui_button("Reset GPU baseline counters", -1, 0)) {
        render_perf_reset_stats();
        gpu_accel_reset_mirror_stats(goxel.gpu_accel);
    }

}
