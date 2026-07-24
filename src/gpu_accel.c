#include "goxel.h"

#include "gpu_accel.h"
#include "../ext_src/stb/stb_ds.h"
#include <limits.h>

#define GPU_MIRROR_TILE_BYTES (TILE_SIZE * TILE_SIZE * TILE_SIZE * 4)
#define GPU_MIRROR_CAPACITY_BYTES (64u * 1024u * 1024u)
#define GPU_MIRROR_SLOTS (GPU_MIRROR_CAPACITY_BYTES / GPU_MIRROR_TILE_BYTES)

typedef struct mirror_entry {
    int pos[3];
    uint64_t tile_id;
    uint64_t seen_epoch;
    uint64_t used_epoch;
    uint64_t uploaded_epoch;
    int slot;
    const void *source_data;
    UT_hash_handle hh;
} mirror_entry_t;

typedef struct {
    int v[3];
} mirror_pos_t;

struct gpu_accel {
    gpu_accel_mode_t mode;
    gpu_capabilities_t capabilities;
    void *metal_mirror;
    mirror_entry_t *entries;
    mirror_entry_t *slots[GPU_MIRROR_SLOTS];
    uint64_t epoch;
    gpu_mirror_stats_t stats;
};

#ifdef __APPLE__
void gpu_accel_query_metal(gpu_capabilities_t *capabilities);
void *gpu_accel_metal_mirror_create(size_t size);
void gpu_accel_metal_mirror_destroy(void *mirror);
bool gpu_accel_metal_mirror_upload(void *mirror, size_t offset,
                                   const void *data, size_t size);
uint64_t gpu_accel_metal_mirror_checksum(void *mirror, size_t offset,
                                         size_t size);
bool gpu_accel_metal_count_block_faces(void *mirror, const void *voxels,
                                       uint32_t *face_count);
bool gpu_accel_metal_emit_block_faces(void *mirror, const void *voxels,
                                      uint32_t *faces, uint32_t capacity,
                                      uint32_t *face_count);
bool gpu_accel_metal_emit_block_vertices(void *mirror, const void *voxels,
                                         void *vertices, uint32_t capacity,
                                         uint32_t *face_count);
bool gpu_accel_metal_emit_mc_vertices(void *mirror, const void *voxels,
                                      void *vertices, uint32_t capacity,
                                      uint32_t *triangle_count);
bool gpu_accel_metal_render_path_preview(
        void *mirror, const int32_t *hash_table, uint32_t hash_capacity,
        const int32_t bounds[2][4],
        const gpu_path_preview_params_t *params, uint8_t *rgba);
#endif

static uint64_t checksum(const void *ptr, size_t size)
{
    const uint8_t *data = ptr;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static mirror_entry_t *find_entry(gpu_accel_t *accel, const int pos[3])
{
    mirror_entry_t *entry = NULL;
    HASH_FIND(hh, accel->entries, pos, sizeof(entry->pos), entry);
    return entry;
}

static void remove_entry(gpu_accel_t *accel, mirror_entry_t *entry)
{
    if (!entry) return;
    HASH_DEL(accel->entries, entry);
    if (entry->slot >= 0)
        accel->slots[entry->slot] = NULL;
    free(entry);
}

static mirror_entry_t *allocate_entry(gpu_accel_t *accel, const int pos[3])
{
    mirror_entry_t *entry;
    int slot;
    for (slot = 0; slot < GPU_MIRROR_SLOTS; slot++)
        if (!accel->slots[slot]) break;
    if (slot == GPU_MIRROR_SLOTS) {
        mirror_entry_t *it, *oldest = NULL;
        HASH_ITER(hh, accel->entries, it, entry) {
            if (!oldest || it->used_epoch < oldest->used_epoch)
                oldest = it;
        }
        if (!oldest) return NULL;
        slot = oldest->slot;
        remove_entry(accel, oldest);
        accel->stats.evictions++;
    }
    entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;
    memcpy(entry->pos, pos, sizeof(entry->pos));
    entry->tile_id = UINT64_MAX;
    entry->slot = slot;
    accel->slots[slot] = entry;
    HASH_ADD(hh, accel->entries, pos, sizeof(entry->pos), entry);
    return entry;
}

static bool upload_entry(gpu_accel_t *accel, mirror_entry_t *entry)
{
#ifdef __APPLE__
    uint64_t cpu_hash, gpu_hash;
    size_t offset = (size_t)entry->slot * GPU_MIRROR_TILE_BYTES;
    if (!entry->source_data ||
            !gpu_accel_metal_mirror_upload(accel->metal_mirror, offset,
                                            entry->source_data,
                                            GPU_MIRROR_TILE_BYTES))
        return false;
    entry->uploaded_epoch = accel->epoch;
    accel->stats.uploads++;
    accel->stats.uploaded_bytes += GPU_MIRROR_TILE_BYTES;
    if (accel->mode == GPU_ACCEL_VALIDATION) {
        cpu_hash = checksum(entry->source_data, GPU_MIRROR_TILE_BYTES);
        gpu_hash = gpu_accel_metal_mirror_checksum(
                accel->metal_mirror, offset, GPU_MIRROR_TILE_BYTES);
        if (cpu_hash != gpu_hash) {
            accel->stats.checksum_mismatches++;
            LOG_E("GPU tile mirror checksum mismatch at %d,%d,%d",
                  entry->pos[0], entry->pos[1], entry->pos[2]);
            return false;
        }
    }
    return true;
#else
    (void)accel;
    (void)entry;
    return false;
#endif
}

gpu_accel_t *gpu_accel_create(gpu_accel_mode_t mode)
{
    gpu_accel_t *accel = calloc(1, sizeof(*accel));
    if (!accel) {
        LOG_E("GPU acceleration scaffold allocation failed; using CPU");
        return NULL;
    }
    accel->mode = mode;
    snprintf(accel->capabilities.reason,
             sizeof(accel->capabilities.reason),
             "No supported GPU compute backend");
#ifdef __APPLE__
    gpu_accel_query_metal(&accel->capabilities);
    if (mode != GPU_ACCEL_OFF && accel->capabilities.available) {
        accel->metal_mirror =
            gpu_accel_metal_mirror_create(GPU_MIRROR_CAPACITY_BYTES);
        if (!accel->metal_mirror) {
            snprintf(accel->capabilities.reason,
                     sizeof(accel->capabilities.reason),
                     "Metal mirror allocation failed; CPU fallback active");
        } else {
            accel->capabilities.mesh_blocks = true;
            accel->capabilities.mesh_marching_cubes = true;
            accel->capabilities.tool_preview = true;
            accel->capabilities.pathtrace_preview = true;
            snprintf(accel->capabilities.reason,
                     sizeof(accel->capabilities.reason),
                     "Metal block and smooth marching-cubes meshing active; "
                     "tool and path previews accelerated; CPU fallback active");
        }
    }
#endif
    accel->stats.capacity_bytes =
        accel->metal_mirror ? GPU_MIRROR_CAPACITY_BYTES : 0;
    if (mode == GPU_ACCEL_OFF) {
        LOG_I("GPU acceleration: Off (CPU/OpenGL fallback)");
    } else {
        LOG_I("GPU acceleration: %s, backend=%s, device=%s, available=%s",
              gpu_accel_mode_name(mode),
              gpu_accel_backend_name(accel->capabilities.backend),
              accel->capabilities.device_name[0] ?
                  accel->capabilities.device_name : "none",
              accel->capabilities.available ? "yes" : "no");
        if (accel->capabilities.reason[0])
            LOG_I("GPU acceleration note: %s", accel->capabilities.reason);
    }
    return accel;
}

void gpu_accel_destroy(gpu_accel_t *accel)
{
    mirror_entry_t *entry, *tmp;
    if (!accel) return;
    HASH_ITER(hh, accel->entries, entry, tmp)
        remove_entry(accel, entry);
#ifdef __APPLE__
    gpu_accel_metal_mirror_destroy(accel->metal_mirror);
#endif
    free(accel);
}

const gpu_capabilities_t *gpu_accel_get_capabilities(
        const gpu_accel_t *accel)
{
    return accel ? &accel->capabilities : NULL;
}

gpu_accel_mode_t gpu_accel_get_mode(const gpu_accel_t *accel)
{
    return accel ? accel->mode : GPU_ACCEL_OFF;
}

const char *gpu_accel_mode_name(gpu_accel_mode_t mode)
{
    switch (mode) {
    case GPU_ACCEL_OFF: return "Off";
    case GPU_ACCEL_AUTO: return "Auto";
    case GPU_ACCEL_VALIDATION: return "Validation";
    }
    return "Unknown";
}

const char *gpu_accel_backend_name(gpu_backend_t backend)
{
    switch (backend) {
    case GPU_BACKEND_NONE: return "CPU/OpenGL";
    case GPU_BACKEND_METAL: return "Metal";
    }
    return "Unknown";
}

static int compare_positions(const void *a, const void *b)
{
    const int *pa = a, *pb = b;
    int axis;
    for (axis = 0; axis < 3; axis++) {
        if (pa[axis] < pb[axis]) return -1;
        if (pa[axis] > pb[axis]) return 1;
    }
    return 0;
}

int gpu_accel_expand_tile_halo(const int (*dirty)[3], int dirty_count,
                               int (*out)[3], int out_capacity)
{
    int i, x, y, z, generated = 0, count = 0;
    int (*positions)[3];
    size_t position_count, bytes;
    if (dirty_count < 0 || out_capacity < 0 ||
            (dirty_count && (!dirty || !out)))
        return -1;
    if (!dirty_count) return 0;
    if (!gpu_accel_checked_mul_size((size_t)dirty_count, 27,
                                    &position_count) ||
            !gpu_accel_checked_mul_size(position_count,
                                        sizeof(*positions), &bytes))
        return -1;
    positions = malloc(bytes);
    if (!positions) return -1;
    for (i = 0; i < dirty_count; i++) {
        for (z = -1; z <= 1; z++)
        for (y = -1; y <= 1; y++)
        for (x = -1; x <= 1; x++) {
            positions[generated][0] = dirty[i][0] + x * TILE_SIZE;
            positions[generated][1] = dirty[i][1] + y * TILE_SIZE;
            positions[generated][2] = dirty[i][2] + z * TILE_SIZE;
            generated++;
        }
    }
    qsort(positions, position_count, sizeof(*positions), compare_positions);
    for (i = 0; i < generated; i++) {
        if (i && !memcmp(positions[i], positions[i - 1],
                         sizeof(*positions)))
            continue;
        if (count == out_capacity) {
            free(positions);
            return -1;
        }
        memcpy(out[count++], positions[i], sizeof(*positions));
    }
    free(positions);
    return count;
}

bool gpu_accel_sync_volume(gpu_accel_t *accel, const struct volume *volume)
{
    volume_iterator_t iter;
    mirror_entry_t *entry, *tmp;
    int pos[3], (*halo)[3] = NULL;
    mirror_pos_t *dirty = NULL;
    int dirty_count = 0, halo_count, halo_capacity, i;
    size_t halo_size;
    uint64_t tile_id;
    const void *data;
    bool ok = true;

    if (!accel || !volume || accel->mode == GPU_ACCEL_OFF ||
            !accel->metal_mirror)
        return false;
    accel->epoch++;
    accel->stats.syncs++;
    accel->stats.source_tiles = 0;

    iter = volume_get_iterator(volume, VOLUME_ITER_TILES);
    while (volume_iter(&iter, pos)) {
        accel->stats.source_tiles++;
        data = volume_get_tile_data(volume, NULL, pos, &tile_id);
        if (!data) continue;
        entry = find_entry(accel, pos);
        if (!entry) entry = allocate_entry(accel, pos);
        if (!entry) {
            ok = false;
            continue;
        }
        entry->seen_epoch = accel->epoch;
        entry->used_epoch = accel->epoch;
        entry->source_data = data;
        if (entry->tile_id != tile_id) {
            entry->tile_id = tile_id;
            arrput(dirty, ((mirror_pos_t){{pos[0], pos[1], pos[2]}}));
            dirty_count++;
            if (!upload_entry(accel, entry)) ok = false;
        }
    }

    HASH_ITER(hh, accel->entries, entry, tmp) {
        if (entry->seen_epoch == accel->epoch) continue;
        arrput(dirty, ((mirror_pos_t){{entry->pos[0], entry->pos[1],
                                      entry->pos[2]}}));
        dirty_count++;
        remove_entry(accel, entry);
        accel->stats.removals++;
    }

    if (!gpu_accel_checked_mul_size((size_t)dirty_count, 27, &halo_size) ||
            halo_size > INT_MAX) {
        ok = false;
        halo_capacity = 0;
    } else {
        halo_capacity = (int)halo_size;
    }
    if (halo_capacity) {
        arrsetlen(halo, halo_capacity);
        halo_count = gpu_accel_expand_tile_halo(
                (const int (*)[3])dirty, dirty_count, halo, halo_capacity);
        if (halo_count < 0) {
            ok = false;
            halo_count = 0;
        }
        for (i = 0; i < halo_count; i++) {
            entry = find_entry(accel, halo[i]);
            if (!entry || entry->uploaded_epoch == accel->epoch) continue;
            if (!upload_entry(accel, entry)) ok = false;
        }
    } else {
        halo_count = 0;
    }
    accel->stats.dirty_tiles += dirty_count;
    accel->stats.halo_tiles += halo_count;
    accel->stats.mirrored_tiles = HASH_COUNT(accel->entries);
    accel->stats.used_bytes =
        accel->stats.mirrored_tiles * GPU_MIRROR_TILE_BYTES;
    arrfree(dirty);
    arrfree(halo);
    return ok;
}

void gpu_accel_get_mirror_stats(const gpu_accel_t *accel,
                                gpu_mirror_stats_t *stats)
{
    if (!stats) return;
    if (accel) *stats = accel->stats;
    else memset(stats, 0, sizeof(*stats));
}

void gpu_accel_reset_mirror_stats(gpu_accel_t *accel)
{
    size_t capacity, used, path_memory;
    uint64_t mirrored;
    if (!accel) return;
    capacity = accel->stats.capacity_bytes;
    used = accel->stats.used_bytes;
    path_memory = accel->stats.path_preview_memory_bytes;
    mirrored = accel->stats.mirrored_tiles;
    memset(&accel->stats, 0, sizeof(accel->stats));
    accel->stats.capacity_bytes = capacity;
    accel->stats.used_bytes = used;
    accel->stats.path_preview_memory_bytes = path_memory;
    accel->stats.mirrored_tiles = mirrored;
}

bool gpu_accel_count_block_faces(gpu_accel_t *accel,
                                 const struct volume *volume,
                                 const int tile_pos[3], int *face_count)
{
#ifdef __APPLE__
    uint8_t voxels[18 * 18 * 18 * 4];
    uint32_t count = 0;
    double start;
    int origin[3] = {
        tile_pos[0] - 1, tile_pos[1] - 1, tile_pos[2] - 1
    };
    const int size[3] = {18, 18, 18};
    if (!accel || !volume || !tile_pos || !face_count ||
            accel->mode == GPU_ACCEL_OFF || !accel->metal_mirror)
        return false;
    accel->stats.mesh_attempts++;
    volume_read(volume, origin, size, voxels);
    start = sys_get_time();
    if (!gpu_accel_metal_count_block_faces(
                accel->metal_mirror, voxels, &count)) {
        accel->stats.mesh_fallbacks++;
        return false;
    }
    accel->stats.mesh_gpu_ms += (sys_get_time() - start) * 1000.0;
    if (count > 6 * TILE_SIZE * TILE_SIZE * TILE_SIZE) {
        accel->stats.mesh_overflows++;
        accel->stats.mesh_fallbacks++;
        return false;
    }
    *face_count = (int)count;
    accel->stats.mesh_successes++;
    return true;
#else
    (void)accel;
    (void)volume;
    (void)tile_pos;
    (void)face_count;
    return false;
#endif
}

bool gpu_accel_validate_block_face_count(gpu_accel_t *accel,
                                         const struct volume *volume,
                                         const int tile_pos[3],
                                         int cpu_face_count)
{
    int gpu_face_count;
    if (!accel || accel->mode != GPU_ACCEL_VALIDATION) return true;
    if (!gpu_accel_count_block_faces(accel, volume, tile_pos,
                                     &gpu_face_count))
        return false;
    if (gpu_face_count != cpu_face_count) {
        accel->stats.mesh_validation_mismatches++;
        accel->stats.mesh_fallbacks++;
        LOG_E("Metal block face-count mismatch at %d,%d,%d: CPU=%d GPU=%d",
              tile_pos[0], tile_pos[1], tile_pos[2],
              cpu_face_count, gpu_face_count);
        return false;
    }
    return true;
}

bool gpu_accel_emit_block_faces(gpu_accel_t *accel,
                                const struct volume *volume,
                                const int tile_pos[3], uint32_t *faces,
                                int capacity, int *face_count)
{
#ifdef __APPLE__
    uint8_t voxels[18 * 18 * 18 * 4];
    uint32_t count = 0;
    double start;
    int origin[3] = {
        tile_pos[0] - 1, tile_pos[1] - 1, tile_pos[2] - 1
    };
    const int size[3] = {18, 18, 18};
    if (!accel || !volume || !tile_pos || !faces || !face_count ||
            capacity <= 0 || accel->mode == GPU_ACCEL_OFF ||
            !accel->metal_mirror)
        return false;
    accel->stats.mesh_attempts++;
    volume_read(volume, origin, size, voxels);
    start = sys_get_time();
    if (!gpu_accel_metal_emit_block_faces(
                accel->metal_mirror, voxels, faces, (uint32_t)capacity,
                &count)) {
        accel->stats.mesh_fallbacks++;
        if (count > (uint32_t)capacity) accel->stats.mesh_overflows++;
        return false;
    }
    accel->stats.mesh_gpu_ms += (sys_get_time() - start) * 1000.0;
    *face_count = (int)count;
    accel->stats.mesh_successes++;
    return true;
#else
    (void)accel; (void)volume; (void)tile_pos; (void)faces;
    (void)capacity; (void)face_count;
    return false;
#endif
}

static int compare_u32(const void *a, const void *b)
{
    uint32_t aa = *(const uint32_t *)a, bb = *(const uint32_t *)b;
    return aa < bb ? -1 : aa > bb;
}

bool gpu_accel_validate_emitted_faces(gpu_accel_t *accel,
                                      const struct volume *volume,
                                      const int tile_pos[3],
                                      const void *vertices_,
                                      int cpu_face_count)
{
    const voxel_vertex_t *vertices = vertices_;
    uint32_t *gpu_faces, *cpu_faces;
    int gpu_count, i;
    bool matches;
    if (!accel || accel->mode != GPU_ACCEL_VALIDATION) return true;
    if (cpu_face_count < 0 || cpu_face_count > 24576 || !vertices)
        return false;
    gpu_faces = malloc(24576 * sizeof(*gpu_faces));
    cpu_faces = malloc(max(cpu_face_count, 1) * sizeof(*cpu_faces));
    if (!gpu_faces || !cpu_faces) {
        free(gpu_faces);
        free(cpu_faces);
        accel->stats.mesh_fallbacks++;
        return false;
    }
    if (!gpu_accel_emit_block_faces(accel, volume, tile_pos, gpu_faces,
                                    24576, &gpu_count)) {
        free(gpu_faces);
        free(cpu_faces);
        return false;
    }
    for (i = 0; i < cpu_face_count; i++)
        cpu_faces[i] = vertices[i * 4].pos_data;
    qsort(gpu_faces, gpu_count, sizeof(*gpu_faces), compare_u32);
    qsort(cpu_faces, cpu_face_count, sizeof(*cpu_faces), compare_u32);
    matches = gpu_count == cpu_face_count &&
              !memcmp(gpu_faces, cpu_faces,
                      cpu_face_count * sizeof(*cpu_faces));
    if (!matches) {
        accel->stats.mesh_validation_mismatches++;
        accel->stats.mesh_fallbacks++;
        LOG_E("Metal block descriptor mismatch at %d,%d,%d: CPU=%d GPU=%d",
              tile_pos[0], tile_pos[1], tile_pos[2],
              cpu_face_count, gpu_count);
    }
    free(gpu_faces);
    free(cpu_faces);
    return matches;
}

bool gpu_accel_generate_block_vertices(gpu_accel_t *accel,
                                       const struct volume *volume,
                                       const int tile_pos[3], void *vertices,
                                       int capacity, int *face_count)
{
#ifdef __APPLE__
    uint8_t voxels[18 * 18 * 18 * 4];
    uint32_t count = 0;
    double start;
    int origin[3] = {
        tile_pos[0] - 1, tile_pos[1] - 1, tile_pos[2] - 1
    };
    const int size[3] = {18, 18, 18};
    if (!accel || !volume || !tile_pos || !vertices || !face_count ||
            capacity <= 0 || accel->mode == GPU_ACCEL_OFF ||
            !accel->metal_mirror || sizeof(voxel_vertex_t) != 36)
        return false;
    accel->stats.mesh_attempts++;
    volume_read(volume, origin, size, voxels);
    start = sys_get_time();
    if (!gpu_accel_metal_emit_block_vertices(
                accel->metal_mirror, voxels, vertices, (uint32_t)capacity,
                &count)) {
        accel->stats.mesh_fallbacks++;
        if (count > (uint32_t)capacity) accel->stats.mesh_overflows++;
        return false;
    }
    accel->stats.mesh_gpu_ms += (sys_get_time() - start) * 1000.0;
    *face_count = (int)count;
    accel->stats.mesh_successes++;
    return true;
#else
    (void)accel; (void)volume; (void)tile_pos; (void)vertices;
    (void)capacity; (void)face_count;
    return false;
#endif
}

static int compare_quad(const void *a, const void *b)
{
    const voxel_vertex_t *qa = a, *qb = b;
    return qa->pos_data < qb->pos_data ? -1 : qa->pos_data > qb->pos_data;
}

bool gpu_accel_validate_block_vertices(gpu_accel_t *accel,
                                       const struct volume *volume,
                                       const int tile_pos[3],
                                       const void *cpu_vertices_,
                                       int cpu_face_count)
{
    const voxel_vertex_t *cpu_vertices = cpu_vertices_;
    voxel_vertex_t *gpu_vertices, *cpu_sorted;
    int gpu_count;
    bool matches;
    size_t bytes;
    if (!accel || accel->mode != GPU_ACCEL_VALIDATION) return true;
    if (!cpu_vertices || cpu_face_count < 0 || cpu_face_count > 24576)
        return false;
    bytes = (size_t)max(cpu_face_count, 1) * 4 * sizeof(*cpu_vertices);
    gpu_vertices = calloc(24576 * 4, sizeof(*gpu_vertices));
    cpu_sorted = malloc(bytes);
    if (!gpu_vertices || !cpu_sorted) {
        free(gpu_vertices); free(cpu_sorted);
        accel->stats.mesh_fallbacks++;
        return false;
    }
    memcpy(cpu_sorted, cpu_vertices,
           (size_t)cpu_face_count * 4 * sizeof(*cpu_vertices));
    if (!gpu_accel_generate_block_vertices(
                accel, volume, tile_pos, gpu_vertices, 24576, &gpu_count)) {
        free(gpu_vertices); free(cpu_sorted);
        return false;
    }
    qsort(gpu_vertices, gpu_count, 4 * sizeof(*gpu_vertices), compare_quad);
    qsort(cpu_sorted, cpu_face_count, 4 * sizeof(*cpu_sorted), compare_quad);
    matches = gpu_count == cpu_face_count &&
        !memcmp(gpu_vertices, cpu_sorted,
                (size_t)cpu_face_count * 4 * sizeof(*gpu_vertices));
    if (!matches) {
        const uint8_t *ga = (const uint8_t *)gpu_vertices;
        const uint8_t *ca = (const uint8_t *)cpu_sorted;
        size_t compare_bytes =
            (size_t)min(gpu_count, cpu_face_count) * 4 *
            sizeof(*gpu_vertices);
        size_t diff = 0;
        while (diff < compare_bytes && ga[diff] == ca[diff]) diff++;
        accel->stats.mesh_validation_mismatches++;
        accel->stats.mesh_fallbacks++;
        LOG_E("Metal block vertex mismatch at %d,%d,%d: CPU=%d GPU=%d",
              tile_pos[0], tile_pos[1], tile_pos[2],
              cpu_face_count, gpu_count);
        if (diff < compare_bytes)
            LOG_E("First vertex byte difference: offset=%llu CPU=%u GPU=%u",
                  (unsigned long long)diff, ca[diff], ga[diff]);
    }
    free(gpu_vertices); free(cpu_sorted);
    return matches;
}

bool gpu_accel_generate_mc_vertices(gpu_accel_t *accel,
                                    const struct volume *volume,
                                    const int tile_pos[3], void *vertices,
                                    int capacity, int *triangle_count)
{
#ifdef __APPLE__
    uint8_t voxels[18 * 18 * 18 * 4];
    uint32_t count = 0;
    double start;
    int origin[3] = {tile_pos[0]-1, tile_pos[1]-1, tile_pos[2]-1};
    const int size[3] = {18,18,18};
    if (!accel || !volume || !tile_pos || !vertices || !triangle_count ||
            capacity <= 0 || accel->mode == GPU_ACCEL_OFF ||
            !accel->metal_mirror || sizeof(voxel_vertex_t) != 36)
        return false;
    accel->stats.mesh_attempts++;
    volume_read(volume, origin, size, voxels);
    start = sys_get_time();
    if (!gpu_accel_metal_emit_mc_vertices(
                accel->metal_mirror, voxels, vertices, (uint32_t)capacity,
                &count)) {
        accel->stats.mesh_fallbacks++;
        if (count > (uint32_t)capacity) accel->stats.mesh_overflows++;
        return false;
    }
    accel->stats.mesh_gpu_ms += (sys_get_time()-start)*1000.0;
    *triangle_count = (int)count;
    accel->stats.mesh_successes++;
    return true;
#else
    (void)accel;(void)volume;(void)tile_pos;(void)vertices;
    (void)capacity;(void)triangle_count;return false;
#endif
}

static int compare_triangle(const void *a, const void *b)
{
    const voxel_vertex_t *ta = a, *tb = b;
    int i, c;
    for (i=0;i<3;i++) {
        c=memcmp(ta[i].pos,tb[i].pos,3);
        if(c)return c;
    }
    return 0;
}

bool gpu_accel_validate_mc_vertices(gpu_accel_t *accel,
                                    const struct volume *volume,
                                    const int tile_pos[3],
                                    const void *cpu_vertices_,
                                    int cpu_triangle_count)
{
    const voxel_vertex_t *cpu_vertices=cpu_vertices_;
    voxel_vertex_t *gpu_vertices,*cpu_sorted;
    int gpu_count,i,v;
    bool matches=true;
    size_t n=max(cpu_triangle_count,1);
    if(!accel||accel->mode!=GPU_ACCEL_VALIDATION)return true;
    gpu_vertices=calloc(16384*3,sizeof(*gpu_vertices));
    cpu_sorted=malloc(n*3*sizeof(*cpu_sorted));
    if(!gpu_vertices||!cpu_sorted){free(gpu_vertices);free(cpu_sorted);return false;}
    memcpy(cpu_sorted,cpu_vertices,(size_t)cpu_triangle_count*3*sizeof(*cpu_sorted));
    if(!gpu_accel_generate_mc_vertices(accel,volume,tile_pos,gpu_vertices,
                                       16384,&gpu_count)){
        free(gpu_vertices);free(cpu_sorted);return false;
    }
    qsort(gpu_vertices,gpu_count,3*sizeof(*gpu_vertices),compare_triangle);
    qsort(cpu_sorted,cpu_triangle_count,3*sizeof(*cpu_sorted),compare_triangle);
    if(gpu_count!=cpu_triangle_count)matches=false;
    for(i=0;matches&&i<gpu_count;i++)for(v=0;v<3;v++){
        voxel_vertex_t *g=&gpu_vertices[i*3+v],*c=&cpu_sorted[i*3+v];
        if(memcmp(g->pos,c->pos,3)||memcmp(g->color,c->color,4)||
           abs(g->normal[0]-c->normal[0])>1||
           abs(g->normal[1]-c->normal[1])>1||
           abs(g->normal[2]-c->normal[2])>1)matches=false;
    }
    if(!matches){
        accel->stats.mesh_validation_mismatches++;
        accel->stats.mesh_fallbacks++;
        LOG_E("Metal marching-cubes mismatch at %d,%d,%d: CPU=%d GPU=%d",
              tile_pos[0],tile_pos[1],tile_pos[2],cpu_triangle_count,gpu_count);
    }
    free(gpu_vertices);free(cpu_sorted);return matches;
}

static uint32_t path_hash_pos(const int pos[3])
{
    return (uint32_t)pos[0] * UINT32_C(73856093) ^
           (uint32_t)pos[1] * UINT32_C(19349663) ^
           (uint32_t)pos[2] * UINT32_C(83492791);
}

bool gpu_accel_render_path_preview(
        gpu_accel_t *accel, const struct volume *volume,
        const gpu_path_preview_params_t *params, uint8_t *rgba)
{
#ifdef __APPLE__
    mirror_entry_t *entry, *tmp;
    int32_t (*table)[4] = NULL;
    int32_t bounds[2][4] = {};
    uint32_t count, capacity = 2, index;
    double start;
    bool first = true, ok;
    if (!accel || !volume || !params || !rgba ||
            accel->mode == GPU_ACCEL_OFF || !accel->metal_mirror ||
            params->width <= 0 || params->height <= 0 ||
            params->max_steps <= 0)
        return false;
    accel->stats.path_preview_attempts++;
    if (params->sample == 0) accel->stats.path_preview_resets++;
    if (!gpu_accel_sync_volume(accel, volume)) {
        accel->stats.path_preview_fallbacks++;
        return false;
    }
    count = HASH_COUNT(accel->entries);
    while (capacity < max(count * 2, 2u) && capacity < 8192)
        capacity <<= 1;
    if (count * 2 > capacity) {
        accel->stats.path_preview_fallbacks++;
        return false;
    }
    table = malloc((size_t)capacity * sizeof(*table));
    if (!table) {
        accel->stats.path_preview_fallbacks++;
        return false;
    }
    for (index = 0; index < capacity; index++)
        table[index][3] = -1;
    HASH_ITER(hh, accel->entries, entry, tmp) {
        if (first) {
            memcpy(bounds[0], entry->pos, sizeof(entry->pos));
            memcpy(bounds[1], entry->pos, sizeof(entry->pos));
            first = false;
        } else {
            int axis;
            for (axis = 0; axis < 3; axis++) {
                bounds[0][axis] = min(bounds[0][axis], entry->pos[axis]);
                bounds[1][axis] = max(bounds[1][axis], entry->pos[axis]);
            }
        }
        index = path_hash_pos(entry->pos) & (capacity - 1);
        while (table[index][3] >= 0) index = (index + 1) & (capacity - 1);
        table[index][0] = entry->pos[0];
        table[index][1] = entry->pos[1];
        table[index][2] = entry->pos[2];
        table[index][3] = entry->slot;
    }
    if (first) {
        bounds[1][0] = bounds[1][1] = bounds[1][2] = 1;
    } else {
        bounds[1][0] += TILE_SIZE;
        bounds[1][1] += TILE_SIZE;
        bounds[1][2] += TILE_SIZE;
    }
    start = sys_get_time();
    accel->stats.path_preview_memory_bytes =
        (size_t)params->width * params->height *
            (4 * sizeof(float) + 4) +
        (size_t)capacity * 4 * sizeof(int32_t);
    ok = gpu_accel_metal_render_path_preview(
            accel->metal_mirror, &table[0][0], capacity, bounds, params, rgba);
    accel->stats.path_preview_gpu_ms +=
        (sys_get_time() - start) * 1000.0;
    if (ok) accel->stats.path_preview_frames++;
    else accel->stats.path_preview_fallbacks++;
    free(table);
    return ok;
#else
    (void)accel; (void)volume; (void)params; (void)rgba;
    return false;
#endif
}

bool gpu_accel_checked_mul_size(size_t a, size_t b, size_t *out)
{
    if (!out || (a && b > SIZE_MAX / a)) return false;
    *out = a * b;
    return true;
}

bool gpu_accel_checked_add_size(size_t a, size_t b, size_t *out)
{
    if (!out || b > SIZE_MAX - a) return false;
    *out = a + b;
    return true;
}
