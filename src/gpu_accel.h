#ifndef GPU_ACCEL_H
#define GPU_ACCEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct volume;

typedef enum {
    GPU_ACCEL_OFF = 0,
    GPU_ACCEL_AUTO,
    GPU_ACCEL_VALIDATION,
} gpu_accel_mode_t;

typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_METAL,
} gpu_backend_t;

typedef struct {
    bool available;
    bool mesh_blocks;
    bool mesh_marching_cubes;
    bool tool_preview;
    bool pathtrace_preview;
    gpu_backend_t backend;
    char device_name[128];
    char reason[192];
} gpu_capabilities_t;

typedef struct gpu_accel gpu_accel_t;

typedef struct {
    uint64_t syncs;
    uint64_t source_tiles;
    uint64_t mirrored_tiles;
    uint64_t dirty_tiles;
    uint64_t halo_tiles;
    uint64_t uploads;
    uint64_t uploaded_bytes;
    uint64_t removals;
    uint64_t evictions;
    uint64_t checksum_mismatches;
    size_t capacity_bytes;
    size_t used_bytes;
    uint64_t mesh_attempts;
    uint64_t mesh_successes;
    uint64_t mesh_fallbacks;
    uint64_t mesh_validation_mismatches;
    uint64_t mesh_overflows;
    double mesh_gpu_ms;
    uint64_t path_preview_attempts;
    uint64_t path_preview_frames;
    uint64_t path_preview_fallbacks;
    uint64_t path_preview_resets;
    double path_preview_gpu_ms;
    size_t path_preview_memory_bytes;
} gpu_mirror_stats_t;

typedef struct {
    int width;
    int height;
    int sample;
    int max_steps;
    float ray_origins[4][4];
    float ray_directions[4][4];
    float light_direction[4];
    float background[4];
    float light_intensity;
    float ambient;
} gpu_path_preview_params_t;

gpu_accel_t *gpu_accel_create(gpu_accel_mode_t mode);
void gpu_accel_destroy(gpu_accel_t *accel);
const gpu_capabilities_t *gpu_accel_get_capabilities(
        const gpu_accel_t *accel);
gpu_accel_mode_t gpu_accel_get_mode(const gpu_accel_t *accel);
const char *gpu_accel_mode_name(gpu_accel_mode_t mode);
const char *gpu_accel_backend_name(gpu_backend_t backend);

// Keep a bounded, sparse GPU copy of the currently rendered volume.
bool gpu_accel_sync_volume(gpu_accel_t *accel, const struct volume *volume);
bool gpu_accel_count_block_faces(gpu_accel_t *accel,
                                 const struct volume *volume,
                                 const int tile_pos[3], int *face_count);
bool gpu_accel_validate_block_face_count(gpu_accel_t *accel,
                                         const struct volume *volume,
                                         const int tile_pos[3],
                                         int cpu_face_count);
bool gpu_accel_emit_block_faces(gpu_accel_t *accel,
                                const struct volume *volume,
                                const int tile_pos[3], uint32_t *faces,
                                int capacity, int *face_count);
bool gpu_accel_validate_emitted_faces(gpu_accel_t *accel,
                                      const struct volume *volume,
                                      const int tile_pos[3],
                                      const void *vertices,
                                      int cpu_face_count);
bool gpu_accel_generate_block_vertices(gpu_accel_t *accel,
                                       const struct volume *volume,
                                       const int tile_pos[3], void *vertices,
                                       int capacity, int *face_count);
bool gpu_accel_validate_block_vertices(gpu_accel_t *accel,
                                       const struct volume *volume,
                                       const int tile_pos[3],
                                       const void *cpu_vertices,
                                       int cpu_face_count);
bool gpu_accel_generate_mc_vertices(gpu_accel_t *accel,
                                    const struct volume *volume,
                                    const int tile_pos[3], void *vertices,
                                    int capacity, int *triangle_count);
bool gpu_accel_validate_mc_vertices(gpu_accel_t *accel,
                                    const struct volume *volume,
                                    const int tile_pos[3],
                                    const void *cpu_vertices,
                                    int cpu_triangle_count);
bool gpu_accel_render_path_preview(
        gpu_accel_t *accel, const struct volume *volume,
        const gpu_path_preview_params_t *params, uint8_t *rgba);
void gpu_accel_get_mirror_stats(const gpu_accel_t *accel,
                                gpu_mirror_stats_t *stats);
void gpu_accel_reset_mirror_stats(gpu_accel_t *accel);

// Expand tile origins to their complete 3x3x3 (26-neighbor) halo.
// Returns the number of unique positions, or -1 if out_capacity is too small.
int gpu_accel_expand_tile_halo(const int (*dirty)[3], int dirty_count,
                               int (*out)[3], int out_capacity);

// Checked arithmetic used by future buffer allocation paths.
bool gpu_accel_checked_mul_size(size_t a, size_t b, size_t *out);
bool gpu_accel_checked_add_size(size_t a, size_t b, size_t *out);

#endif
