/* Goxel 3D voxels editor
 *
 * copyright (c) 2017 Guillaume Chereau <guillaume@noctua-software.com>
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


typedef struct {
    tool_t tool;

    volume_t *volume_orig; // Original volume.
    volume_t *volume;      // Volume containing only the tool path.

    // Gesture start and last pos (should we put it in the 3d gesture?)
    float start_pos[3];
    float last_pos[3];
    // Cache of the last operation.
    // XXX: could we remove this?
    struct     {
        float      pos[3];
        bool       pressed;
        int        mode;
        uint64_t   volume_key;
    } last_op;

} tool_brush_t;

static bool check_can_skip(
        tool_brush_t *brush, const gesture3d_t *gest, int mode)
{
    volume_t *volume = goxel.tool_volume;
    const bool pressed = gest->flags & GESTURE3D_FLAG_PRESSED;
    if (    pressed == brush->last_op.pressed &&
            mode == brush->last_op.mode &&
            brush->last_op.volume_key == volume_get_key(volume) &&
            vec3_equal(gest->pos, brush->last_op.pos)) {
        return true;
    }
    brush->last_op.pressed = pressed;
    brush->last_op.mode = mode;
    brush->last_op.volume_key = volume_get_key(volume);
    vec3_copy(gest->pos, brush->last_op.pos);
    return false;
}

static void get_box(const float p0[3], const float p1[3], const float n[3],
                    float r, const float plane[4][4], float out[4][4])
{
    float rot[4][4], box[4][4];
    float v[3];

    if (p1 == NULL) {
        bbox_from_extents(box, p0, r, r, r);
        box_swap_axis(box, 2, 0, 1, box);
        mat4_copy(box, out);
        return;
    }
    if (r == 0) {
        bbox_from_points(box, p0, p1);
        bbox_grow(box, 0.5, 0.5, 0.5, box);
        // Apply the plane rotation.
        mat4_copy(plane, rot);
        vec4_set(rot[3], 0, 0, 0, 1);
        mat4_imul(box, rot);
        mat4_copy(box, out);
        return;
    }

    // Create a box for a line:
    int i;
    const float AXES[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    mat4_set_identity(box);
    vec3_mix(p0, p1, 0.5, box[3]);
    vec3_sub(p1, box[3], box[2]);
    for (i = 0; i < 3; i++) {
        vec3_cross(box[2], AXES[i], box[0]);
        if (vec3_norm2(box[0]) > 0) break;
    }
    if (i == 3) {
        mat4_copy(box, out);
        return;
    }
    vec3_normalize(box[0], v);
    vec3_mul(v, r, box[0]);
    vec3_cross(box[2], box[0], v);
    vec3_normalize(v, v);
    vec3_mul(v, r, box[1]);
    mat4_copy(box, out);
}

static int on_drag(gesture3d_t *gest)
{
    tool_brush_t *brush = USER_GET(gest->user, 0);
    painter_t painter = *(painter_t*)USER_GET(gest->user, 1);
    float box[4][4];
    bool shift = gest->flags & GESTURE3D_FLAG_SHIFT;
    float r = goxel.tool_radius;
    int nb, i;
    float pos[3];

    if (gest->state == GESTURE3D_STATE_BEGIN) {
        volume_set(brush->volume_orig, goxel.image->active_layer->volume);
        brush->last_op.mode = 0; // Discard last op.
        vec3_copy(gest->pos, brush->last_pos);
        volume_clear(brush->volume);

        if (shift) {
            painter.shape = &shape_cylinder;
            painter.mode = MODE_MAX;
            vec4_set(painter.color, 255, 255, 255, 255);
            get_box(brush->start_pos, gest->pos, gest->normal, r, NULL, box);
            volume_op(brush->volume, &painter, box);
        }
    }

    painter = *(painter_t*)USER_GET(gest->user, 1);
    if (    (gest->state == GESTURE3D_STATE_UPDATE) &&
            (check_can_skip(brush, gest, painter.mode))) {
        return 0;
    }

    painter.mode = MODE_MAX;
    vec4_set(painter.color, 255, 255, 255, 255);

    // Render several times if the space between the current pos
    // and the last pos is larger than the size of the tool shape.
    nb = ceil(vec3_dist(gest->pos, brush->last_pos) / (2 * r));
    nb = max(nb, 1);
    for (i = 0; i < nb; i++) {
        vec3_mix(brush->last_pos, gest->pos, (i + 1.0) / nb, pos);
        get_box(pos, NULL, gest->normal, r, NULL, box);
        volume_op(brush->volume, &painter, box);
    }

    painter = *(painter_t*)USER_GET(gest->user, 1);
    if (!goxel.tool_volume) goxel.tool_volume = volume_new();
    volume_set(goxel.tool_volume, brush->volume_orig);
    volume_merge(goxel.tool_volume, brush->volume, painter.mode, painter.color);
    vec3_copy(gest->pos, brush->start_pos);
    brush->last_op.volume_key = volume_get_key(goxel.tool_volume);

    if (gest->state == GESTURE3D_STATE_END) {
        volume_set(goxel.image->active_layer->volume, goxel.tool_volume);
        volume_set(brush->volume_orig, goxel.tool_volume);
        volume_delete(goxel.tool_volume);
        goxel.tool_volume = NULL;
        image_history_push(goxel.image);
    }
    vec3_copy(gest->pos, brush->last_pos);
    return 0;
}

static int on_hover(gesture3d_t *gest)
{
    volume_t *volume = goxel.image->active_layer->volume;
    tool_brush_t *brush = USER_GET(gest->user, 0);
    const painter_t *painter = USER_GET(gest->user, 1);
    float box[4][4];
    bool shift = gest->flags & GESTURE3D_FLAG_SHIFT;

    if (gest->state == GESTURE3D_STATE_END || !gest->snaped) {
        volume_delete(goxel.tool_volume);
        goxel.tool_volume = NULL;
        return 0;
    }

    // Update snap offset in case we press 'space'.
    gest->snap_offset = goxel.snap_offset * goxel.tool_radius +
                        ((painter->mode == MODE_OVER) ? 0.5 : -0.5);

    if (shift)
        render_line(&goxel.rend, brush->start_pos, gest->pos, NULL, 0);

    if (goxel.tool_volume && check_can_skip(brush, gest, painter->mode))
        return 0;

    get_box(gest->pos, NULL, gest->normal, goxel.tool_radius, NULL, box);

    if (!goxel.tool_volume) goxel.tool_volume = volume_new();
    volume_set(goxel.tool_volume, volume);
    volume_op(goxel.tool_volume, painter, box);

    brush->last_op.volume_key = volume_get_key(volume);

    return 0;
}


static int iter(tool_t *tool, const painter_t *painter,
                const float viewport[4])
{
    tool_brush_t *brush = (tool_brush_t*)tool;
    float snap_offset;

    if (goxel.snap_mask & (SNAP_SELECTION_IN | SNAP_SELECTION_OUT)) {
        render_box(&goxel.rend, goxel.image->selection_box, NULL,
                   EFFECT_STRIP | EFFECT_WIREFRAME);
    }

    if (!brush->volume_orig)
        brush->volume_orig = volume_copy(goxel.image->active_layer->volume);
    if (!brush->volume)
        brush->volume = volume_new();

    snap_offset = goxel.snap_offset * goxel.tool_radius +
        ((painter->mode == MODE_OVER) ? 0.5 : -0.5);

    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE3D_TYPE_DRAG,
        .buttons_mask = GESTURE3D_FLAG_CTRL,
        .snap_mask = goxel.snap_mask | SNAP_ROUNDED,
        .snap_offset = snap_offset,
        .callback = on_drag,
        .user = USER_PASS(brush, painter),
    });
    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE3D_TYPE_HOVER,
        .buttons_mask = GESTURE3D_FLAG_CTRL,
        .snap_mask = goxel.snap_mask | SNAP_ROUNDED,
        .snap_offset = snap_offset,
        .callback = on_hover,
        .user = USER_PASS(brush, painter),
    });

    return tool->state;
}


static int gui(tool_t *tool)
{
    tool_gui_color();
    tool_gui_radius();
    tool_gui_smoothness();
    tool_gui_shape(NULL);
    return 0;
}

TOOL_REGISTER(TOOL_BRUSH, brush, tool_brush_t,
              .name = N_("Brush"),
              .iter_fn = iter,
              .gui_fn = gui,
              .flags = TOOL_REQUIRE_CAN_EDIT | TOOL_ALLOW_PICK_COLOR,
              .default_shortcut = "B"
)

typedef struct {
    tool_t tool;
    volume_t *volume_orig;
    uint32_t seed;
    float last_pos[3];
    int density;
} tool_noise_brush_t;

static uint32_t noise_hash_int(uint32_t h, int v)
{
    h ^= (uint32_t)v;
    h *= 16777619u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h;
}

static uint32_t noise_hash_pos(const int pos[3], uint32_t seed)
{
    uint32_t h = 2166136261u ^ seed;
    h = noise_hash_int(h, pos[0]);
    h = noise_hash_int(h, pos[1]);
    h = noise_hash_int(h, pos[2]);
    return h;
}

static bool noise_pos_in_image_box(const int pos[3])
{
    float p[3];
    if (box_is_null(goxel.image->box) || !box_is_bbox(goxel.image->box))
        return true;
    vec3_set(p, pos[0] + 0.5, pos[1] + 0.5, pos[2] + 0.5);
    return bbox_contains_vec(goxel.image->box, p);
}

static void noise_apply_voxel(
        volume_t *volume, volume_accessor_t *accessor,
        const int pos[3], const painter_t *painter)
{
    uint8_t old_value[4], new_value[4];

    if (!noise_pos_in_image_box(pos)) return;

    volume_get_at(volume, accessor, pos, old_value);
    memcpy(new_value, painter->color, 4);
    if (painter->mode == MODE_SUB || painter->mode == MODE_SUB_CLAMP) {
        if (!old_value[3]) return;
        memset(new_value, 0, sizeof(new_value));
    } else if (painter->mode == MODE_PAINT) {
        if (!old_value[3]) return;
        new_value[3] = old_value[3];
    }
    volume_set_at(volume, accessor, pos, new_value);
}

static void noise_apply_stamp(
        volume_t *volume, const painter_t *painter, const float pos[3],
        float radius, int density, uint32_t seed)
{
    volume_accessor_t accessor;
    int p[3], min_pos[3], max_pos[3];
    int x, y, z;
    float center[3], dist2;
    float radius2 = radius * radius;

    density = clamp(density, 1, 100);
    accessor = volume_get_accessor(volume);
    for (int i = 0; i < 3; i++) {
        min_pos[i] = floor(pos[i] - radius);
        max_pos[i] = ceil(pos[i] + radius);
    }
    for (z = min_pos[2]; z <= max_pos[2]; z++) {
        for (y = min_pos[1]; y <= max_pos[1]; y++) {
            for (x = min_pos[0]; x <= max_pos[0]; x++) {
                vec3_set(center, x + 0.5, y + 0.5, z + 0.5);
                dist2 = vec3_dist2(pos, center);
                if (dist2 > radius2) continue;
                p[0] = x, p[1] = y, p[2] = z;
                if (noise_hash_pos(p, seed) % 100 >= (uint32_t)density)
                    continue;
                noise_apply_voxel(volume, &accessor, p, painter);
            }
        }
    }
}

static void noise_apply_path(
        volume_t *volume, const painter_t *painter,
        const float p0[3], const float p1[3], uint32_t seed, int density)
{
    float pos[3];
    float radius = max(goxel.tool_radius, 0.5f);
    int i, nb;

    nb = ceil(vec3_dist(p0, p1) / max(radius, 1.0f));
    nb = max(nb, 1);
    for (i = 0; i < nb; i++) {
        vec3_mix(p0, p1, (i + 1.0) / nb, pos);
        noise_apply_stamp(volume, painter, pos, radius, density,
                          seed + i * 2654435761u);
    }
}

static int noise_on_drag(gesture3d_t *gest)
{
    tool_noise_brush_t *brush = USER_GET(gest->user, 0);
    const painter_t *painter = USER_GET(gest->user, 1);

    if (gest->state == GESTURE3D_STATE_BEGIN) {
        if (!brush->volume_orig)
            brush->volume_orig =
                volume_copy(goxel.image->active_layer->volume);
        volume_set(brush->volume_orig, goxel.image->active_layer->volume);
        if (!goxel.tool_volume)
            goxel.tool_volume = volume_new();
        volume_set(goxel.tool_volume, brush->volume_orig);
        brush->seed++;
        vec3_copy(gest->pos, brush->last_pos);
        noise_apply_stamp(goxel.tool_volume, painter, gest->pos,
                          max(goxel.tool_radius, 0.5f), brush->density,
                          brush->seed);
    } else {
        if (!goxel.tool_volume) return 0;
        noise_apply_path(goxel.tool_volume, painter,
                         brush->last_pos, gest->pos,
                         brush->seed, brush->density);
    }

    if (gest->state == GESTURE3D_STATE_END) {
        volume_set(goxel.image->active_layer->volume, goxel.tool_volume);
        volume_set(brush->volume_orig, goxel.tool_volume);
        volume_delete(goxel.tool_volume);
        goxel.tool_volume = NULL;
        image_history_push(goxel.image);
    }
    vec3_copy(gest->pos, brush->last_pos);
    return 0;
}

static int noise_on_hover(gesture3d_t *gest)
{
    volume_t *volume = goxel.image->active_layer->volume;
    tool_noise_brush_t *brush = USER_GET(gest->user, 0);
    const painter_t *painter = USER_GET(gest->user, 1);

    if (gest->state == GESTURE3D_STATE_END || !gest->snaped) {
        volume_delete(goxel.tool_volume);
        goxel.tool_volume = NULL;
        return 0;
    }

    if (!goxel.tool_volume)
        goxel.tool_volume = volume_new();
    volume_set(goxel.tool_volume, volume);
    noise_apply_stamp(goxel.tool_volume, painter, gest->pos,
                      max(goxel.tool_radius, 0.5f), brush->density,
                      0x9e3779b9u);
    return 0;
}

static int noise_iter(tool_t *tool, const painter_t *painter,
                      const float viewport[4])
{
    tool_noise_brush_t *brush = (tool_noise_brush_t*)tool;
    float snap_offset;

    if (!brush->density)
        brush->density = 35;

    if (goxel.snap_mask & (SNAP_SELECTION_IN | SNAP_SELECTION_OUT)) {
        render_box(&goxel.rend, goxel.image->selection_box, NULL,
                   EFFECT_STRIP | EFFECT_WIREFRAME);
    }

    snap_offset = goxel.snap_offset * goxel.tool_radius +
        ((painter->mode == MODE_OVER) ? 0.5 : -0.5);

    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE3D_TYPE_DRAG,
        .buttons_mask = GESTURE3D_FLAG_CTRL,
        .snap_mask = goxel.snap_mask | SNAP_ROUNDED,
        .snap_offset = snap_offset,
        .callback = noise_on_drag,
        .user = USER_PASS(brush, painter),
    });
    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE3D_TYPE_HOVER,
        .buttons_mask = GESTURE3D_FLAG_CTRL,
        .snap_mask = goxel.snap_mask | SNAP_ROUNDED,
        .snap_offset = snap_offset,
        .callback = noise_on_hover,
        .user = USER_PASS(brush, painter),
    });

    return tool->state;
}

static int noise_gui(tool_t *tool)
{
    tool_noise_brush_t *brush = (tool_noise_brush_t*)tool;
    tool_gui_color();
    tool_gui_radius();
    if (!brush->density)
        brush->density = 35;
    if (gui_input_int(_("Density"), &brush->density, 1, 100))
        brush->density = clamp(brush->density, 1, 100);
    return 0;
}

TOOL_REGISTER(TOOL_NOISE_BRUSH, noise_brush, tool_noise_brush_t,
              .name = N_("Noise Brush"),
              .iter_fn = noise_iter,
              .gui_fn = noise_gui,
              .flags = TOOL_REQUIRE_CAN_EDIT | TOOL_ALLOW_PICK_COLOR,
              .default_shortcut = "N"
)
