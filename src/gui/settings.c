/* Goxel 3D voxels editor
 *
 * copyright (c) 2015 Guillaume Chereau <guillaume@noctua-software.com>
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
#include <errno.h> // IWYU pragma: keep.

#include "../ext_src/stb/stb_ds.h"

#include "utils/ini.h"

static int shortcut_callback(action_t *action, void *user)
{
    if (!(action->flags & ACTION_CAN_EDIT_SHORTCUT)) return 0;
    gui_push_id(action->id);
    if (action->help)
        gui_text("%s: %s", action->id, tr(action->help));
    else
        gui_text("%s", action->id);
    gui_next_column();
    // XXX: need to check if the inputs are valid!
    gui_input_text("", action->shortcut, sizeof(action->shortcut));
    if (gui_is_item_deactivated()) settings_save();
    gui_next_column();
    gui_pop_id();
    return 0;
}

static void on_keymap(int idx, keymap_t *keymap)
{
    int i;
    char id[32];
    bool selected;
    const char *preview = "";

    const struct {
        const char *label;
        int input;
    } input_choices[] = {
        { "Right Mouse", GESTURE_RMB },
        { "Middle Mouse", GESTURE_MMB },
        { "Ctrl Right Mouse", GESTURE_CTRL | GESTURE_RMB },
        { "Ctrl Middle Mouse", GESTURE_CTRL | GESTURE_MMB },
        { "Shift Right Mouse", GESTURE_SHIFT | GESTURE_RMB },
        { "Shift Middle Mouse", GESTURE_SHIFT | GESTURE_MMB },
    };

    const char *action_choices[] = { "Pan", "Rotate", "Zoom" };

    if (keymap->action < 0 || keymap->action > 2) {
        return;
    }

    snprintf(id, sizeof(id), "keymap_%d", idx);
    gui_push_id(id);

    if (gui_combo_begin("##action", action_choices[keymap->action])) {
        for (i = 0; i < ARRAY_SIZE(action_choices); i++) {
            selected = i == keymap->action;
            if (gui_combo_item(action_choices[i], selected)) {
                keymap->action = input_choices[i].input;
                settings_save();
            }
        }
        gui_combo_end();
    }
    gui_next_column();

    for (i = 0; i < ARRAY_SIZE(input_choices); i++) {
        if (input_choices[i].input == keymap->input) {
            preview = input_choices[i].label;
            break;
        }
    }

    if (gui_combo_begin("##input", preview)) {
        for (i = 0; i < ARRAY_SIZE(input_choices); i++) {
            selected = input_choices[i].input == keymap->input;
            if (gui_combo_item(input_choices[i].label, selected)) {
                keymap->input = input_choices[i].input;
                settings_save();
            }
        }
        gui_combo_end();
    }

    gui_next_column();

    if (gui_button("Delete", 0, 0)) {
        arrdel(goxel.keymaps, idx);
    }

    gui_next_column();

    gui_pop_id();
}

static void on_add_keymap_button(void)
{
    keymap_t keymap = { 0, GESTURE_RMB };
    arrput(goxel.keymaps, keymap);
    settings_save();
}

static int layout_orientation_from_string(const char *value)
{
    if (strcmp(value, "vertical") == 0)
        return GUI_LAYOUT_VERTICAL;
    return GUI_LAYOUT_HORIZONTAL;
}

static const char *layout_orientation_to_string(int value)
{
    return value == GUI_LAYOUT_VERTICAL ? "vertical" : "horizontal";
}

int gui_settings_popup(void *data)
{
    const char *names[128];
    theme_t *theme;
    int i, nb, current;
    theme_t *themes = theme_get_list();
    int ret = 0;
    const tr_lang_t *language;
    const tr_lang_t *languages;
    bool val;
    float scale;
    const char *path;
    const char *orientations[] = { "Horizontal", "Vertical" };

    if (gui_section_begin(_("Language"), GUI_SECTION_COLLAPSABLE)) {
        language = tr_get_language();
        if (gui_combo_begin("##lang", language->name)) {
            languages = tr_get_supported_languages();
            for (i = 0; languages[i].id; i++) {
                if (gui_combo_item(languages[i].name,
                            &languages[i] == language)) {
                    // Note: we don't change the language yet, we do it in
                    // goxel_iter so not to mess up the UI render.
                    goxel.lang = languages[i].id;
                    settings_save();
                }
            }
            gui_combo_end();
        }
    } gui_section_end();

    if (gui_section_begin(_("UI"), GUI_SECTION_COLLAPSABLE)) {
        DL_COUNT(themes, theme, nb);
        i = 0;
        DL_FOREACH(themes, theme) {
            if (strcmp(theme->name, theme_get()->name) == 0) current = i;
            names[i++] = theme->name;
        }
        gui_text("Theme");
        if (gui_combo("#themes", &current, names, nb)) {
            theme_set(names[current]);
            settings_save();
        }
        scale = gui_get_scale();
        if (gui_input_float("Scale", &scale, 0.1, 0.5, 2.0, "%.1f")) {
            gui_set_scale(scale);
        }
        if (gui_is_item_deactivated()) {
            settings_save();
        }
        gui_text("Top Toolbar");
        current = goxel.gui.topbar_orientation;
        if (gui_combo("##topbar_orientation", &current, orientations, 2)) {
            goxel.gui.topbar_orientation = current;
            settings_save();
        }
        gui_text("MyTools Toolbar");
        current = goxel.gui.mytoolsbar_orientation;
        if (gui_combo("##mytoolsbar_orientation", &current,
                      orientations, 2)) {
            goxel.gui.mytoolsbar_orientation = current;
            settings_save();
        }
        gui_text("Main Toolbar");
        current = goxel.gui.paintbar_orientation;
        if (gui_combo("##paintbar_orientation", &current, orientations, 2)) {
            goxel.gui.paintbar_orientation = current;
            settings_save();
        }
        gui_text("Select Toolbar");
        current = goxel.gui.selectbar_orientation;
        if (gui_combo("##selectbar_orientation", &current, orientations, 2)) {
            goxel.gui.selectbar_orientation = current;
            settings_save();
        }
        gui_text("Swatches Toolbar");
        current = goxel.gui.swatchesbar_orientation;
        if (gui_combo("##swatchesbar_orientation", &current, orientations, 2)) {
            goxel.gui.swatchesbar_orientation = current;
            settings_save();
        }
        gui_text("Navigation Toolbar");
        current = goxel.gui.leftbar_orientation;
        if (gui_combo("##leftbar_orientation", &current, orientations, 2)) {
            goxel.gui.leftbar_orientation = current;
            settings_save();
        }
        if (gui_button("Reset Toolbar Layout", 1.0, 0)) {
            gui_reset_toolbar_layout();
            settings_save();
        }
        if (goxel.gui.reference_image_path[0]) {
            gui_text("Reference Image");
            val = goxel.gui.reference_image_visible;
            if (gui_checkbox("Show Reference Image", &val, NULL)) {
                goxel.gui.reference_image_visible = val;
                if (val)
                    goxel.gui.reference_image_load_failed = false;
                settings_save();
            }
            if (gui_button("Reset Reference Image Position", 1.0, 0)) {
                gui_reset_reference_image_layout();
                settings_save();
            }
            if (gui_button("Clear Reference Image", 1.0, 0)) {
                goxel_reference_image_clear();
                settings_save();
            }
        }

    } gui_section_end();

    if (gui_section_begin("GPU Acceleration",
                          GUI_SECTION_COLLAPSABLE_CLOSED)) {
        const char *gpu_modes[] = { "Off", "Auto", "Validation" };
        current = goxel.gpu_accel_mode;
        gui_text("Mode");
        if (gui_combo("##gpu_acceleration_mode", &current, gpu_modes,
                      ARRAY_SIZE(gpu_modes))) {
            goxel.gpu_accel_mode = (gpu_accel_mode_t)current;
            gpu_accel_destroy(goxel.gpu_accel);
            goxel.gpu_accel = gpu_accel_create(goxel.gpu_accel_mode);
            // Force every cached block tile to be rebuilt by the newly
            // selected backend, otherwise CPU/GPU cache entries can survive
            // a mode change and make the selection appear ineffective.
            render_on_low_memory(&goxel.rend);
            settings_save();
        }
        gui_text("Validation checks every changed tile copied to Metal.");
        gui_text("Rendering remains on the CPU/OpenGL path in Phase 2.");
    } gui_section_end();

    if (gui_section_begin("Inputs", GUI_SECTION_COLLAPSABLE_CLOSED)) {
        val = goxel.emulate_three_buttons_mouse == KEY_LEFT_ALT;
        if (gui_checkbox("Emulate 3 buttons with Alt", &val,
                         "Emulate Middle Mouse with Alt+Left Mouse.")) {
            goxel.emulate_three_buttons_mouse = val ? KEY_LEFT_ALT : 0;
            gesture_set_emulate_three_buttons_mouse(
                    goxel.emulate_three_buttons_mouse);
            settings_save();
        }
    } gui_section_end();

    if (gui_section_begin("XCom", GUI_SECTION_COLLAPSABLE_CLOSED)) {
        gui_text("Gox Repository");
        gui_input_text("##xcom_gox_repository", goxel.xcom_gox_repository,
                       sizeof(goxel.xcom_gox_repository));
        if (gui_is_item_deactivated()) settings_save();
        if (gui_button(_("Browse"), 0, 0)) {
            path = sys_open_folder_dialog(
                    "XCom Gox Repository",
                    goxel.xcom_gox_repository[0] ?
                        goxel.xcom_gox_repository : NULL);
            if (path) {
                snprintf(goxel.xcom_gox_repository,
                         sizeof(goxel.xcom_gox_repository), "%s", path);
                settings_save();
            }
        }
    } gui_section_end();

    if (gui_section_begin(_("Paths"), GUI_SECTION_COLLAPSABLE_CLOSED)) {
        gui_text("Palettes: %s/palettes", sys_get_user_dir());
        gui_text("Progs: %s/progs", sys_get_user_dir());
    } gui_section_end();

    if (gui_section_begin(_("Shortcuts"), GUI_SECTION_COLLAPSABLE_CLOSED)) {
        gui_columns(2);
        gui_separator();
        actions_iter(shortcut_callback, NULL);
        gui_separator();
        gui_columns(1);
    } gui_section_end();

    if (gui_section_begin("Keymaps", GUI_SECTION_COLLAPSABLE_CLOSED)) {
        gui_columns(3);
        gui_separator();
        for (i = 0; i < arrlen(goxel.keymaps); i++) {
            on_keymap(i, &goxel.keymaps[i]);
        }
        gui_separator();
        gui_columns(1);
        if (gui_button(_("Add"), 0, 0)) {
            on_add_keymap_button();
        }
    }
    gui_section_end();

    gui_popup_bottom_begin();
    ret = gui_button(_("OK"), 0, 0);
    gui_popup_bottom_end();
    return ret;
}

static void add_keymap(const char *name, const char *value)
{
    keymap_t keymap = {
        .action = -1,
        .input = 0,
    };

    if (strcmp(name, "pan") == 0) keymap.action = 0;
    if (strcmp(name, "rotate") == 0) keymap.action = 1;
    if (strcmp(name, "zoom") == 0) keymap.action = 2;

    if (strstr(value, "right mouse")) keymap.input |= GESTURE_RMB;
    if (strstr(value, "middle mouse")) keymap.input |= GESTURE_MMB;
    if (strstr(value, "shift")) keymap.input |= GESTURE_SHIFT;
    if (strstr(value, "ctrl")) keymap.input |= GESTURE_CTRL;

    if (keymap.action == -1 || keymap.input == 0) {
        LOG_W("Cannot parse keymap %s = %s", name, value);
        return;
    }

    arrput(goxel.keymaps, keymap);
}

static int settings_ini_handler(void *user, const char *section,
                                const char *name, const char *value,
                                int lineno)
{
    action_t *a;
    if (strcmp(section, "ui") == 0) {
        if (strcmp(name, "theme") == 0) {
            theme_set(value);
        }
        if (strcmp(name, "language") == 0) {
            tr_set_language(value);
            goxel.lang = tr_get_language()->id;
        }
        if (strcmp(name, "scale") == 0) {
            gui_set_scale(atof(value));
        }
        if (strcmp(name, "topbar_orientation") == 0) {
            goxel.gui.topbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "mytoolsbar_orientation") == 0) {
            goxel.gui.mytoolsbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "paintbar_orientation") == 0) {
            goxel.gui.paintbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "selectbar_orientation") == 0) {
            goxel.gui.selectbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "swatchesbar_orientation") == 0) {
            goxel.gui.swatchesbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "leftbar_orientation") == 0) {
            goxel.gui.leftbar_orientation =
                layout_orientation_from_string(value);
        }
        if (strcmp(name, "topbar_folded") == 0) {
            goxel.gui.topbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "mytoolsbar_folded") == 0) {
            goxel.gui.mytoolsbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "paintbar_folded") == 0) {
            goxel.gui.paintbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "selectbar_folded") == 0) {
            goxel.gui.selectbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "swatchesbar_folded") == 0) {
            goxel.gui.swatchesbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "leftbar_folded") == 0) {
            goxel.gui.leftbar_folded = atoi(value) != 0;
        }
        if (strcmp(name, "topbar_x") == 0) {
            goxel.gui.topbar_pos[0] = atof(value);
            goxel.gui.topbar_pos_set = true;
        }
        if (strcmp(name, "topbar_y") == 0) {
            goxel.gui.topbar_pos[1] = atof(value);
            goxel.gui.topbar_pos_set = true;
        }
        if (strcmp(name, "mytoolsbar_x") == 0) {
            goxel.gui.mytoolsbar_pos[0] = atof(value);
            goxel.gui.mytoolsbar_pos_set = true;
        }
        if (strcmp(name, "mytoolsbar_y") == 0) {
            goxel.gui.mytoolsbar_pos[1] = atof(value);
            goxel.gui.mytoolsbar_pos_set = true;
        }
        if (strcmp(name, "paintbar_x") == 0) {
            goxel.gui.paintbar_pos[0] = atof(value);
            goxel.gui.paintbar_pos_set = true;
        }
        if (strcmp(name, "paintbar_y") == 0) {
            goxel.gui.paintbar_pos[1] = atof(value);
            goxel.gui.paintbar_pos_set = true;
        }
        if (strcmp(name, "selectbar_x") == 0) {
            goxel.gui.selectbar_pos[0] = atof(value);
            goxel.gui.selectbar_pos_set = true;
        }
        if (strcmp(name, "selectbar_y") == 0) {
            goxel.gui.selectbar_pos[1] = atof(value);
            goxel.gui.selectbar_pos_set = true;
        }
        if (strcmp(name, "swatchesbar_x") == 0) {
            goxel.gui.swatchesbar_pos[0] = atof(value);
            goxel.gui.swatchesbar_pos_set = true;
        }
        if (strcmp(name, "swatchesbar_y") == 0) {
            goxel.gui.swatchesbar_pos[1] = atof(value);
            goxel.gui.swatchesbar_pos_set = true;
        }
        if (strcmp(name, "leftbar_x") == 0) {
            goxel.gui.leftbar_pos[0] = atof(value);
            goxel.gui.leftbar_pos_set = true;
        }
        if (strcmp(name, "leftbar_y") == 0) {
            goxel.gui.leftbar_pos[1] = atof(value);
            goxel.gui.leftbar_pos_set = true;
        }
    }
    if (strcmp(section, "shortcuts") == 0) {
        a = action_get_by_name(name);
        if (a) {
            strncpy(a->shortcut, value, sizeof(a->shortcut) - 1);
        } else {
            LOG_W("Cannot set shortcut for unknown action '%s'", name);
        }
    }
    if (strcmp(section, "keymaps") == 0) {
        add_keymap(name, value);
    }
    if (strcmp(section, "inputs") == 0) {
        if (strcmp(name, "emulate_three_buttons_mouse") == 0) {
            if (strcmp(value, "alt") == 0) {
                goxel.emulate_three_buttons_mouse = KEY_LEFT_ALT;
            }
        }
    }
    if (strcmp(section, "xcom") == 0) {
        if (strcmp(name, "gox_repository") == 0) {
            snprintf(goxel.xcom_gox_repository,
                     sizeof(goxel.xcom_gox_repository), "%s", value);
        }
    }
    if (strcmp(section, "gpu_acceleration") == 0) {
        if (strcmp(name, "mode") == 0) {
            if (strcasecmp(value, "off") == 0)
                goxel.gpu_accel_mode = GPU_ACCEL_OFF;
            else if (strcasecmp(value, "validation") == 0)
                goxel.gpu_accel_mode = GPU_ACCEL_VALIDATION;
            else
                goxel.gpu_accel_mode = GPU_ACCEL_AUTO;
        }
    }
    if (strcmp(section, "view") == 0) {
        if (strcmp(name, "grid") == 0) {
            if (atoi(value))
                goxel.view_effects |= EFFECT_GRID;
            else
                goxel.view_effects &= ~EFFECT_GRID;
        }
        if (strcmp(name, "edges") == 0) {
            if (atoi(value))
                goxel.view_effects |= EFFECT_EDGES;
            else
                goxel.view_effects &= ~EFFECT_EDGES;
        }
        if (strcmp(name, "frames") == 0) {
            if (atoi(value))
                goxel.view_effects |= EFFECT_FRAMES;
            else
                goxel.view_effects &= ~EFFECT_FRAMES;
        }
        if (strcmp(name, "frame_spacing") == 0) {
            goxel.frame_spacing = clamp(atoi(value), 1, 64);
        }
    }
    if (strcmp(section, "reference") == 0) {
        if (strcmp(name, "visible") == 0) {
            goxel.gui.reference_image_visible = atoi(value) != 0;
        }
        if (strcmp(name, "path") == 0) {
            snprintf(goxel.gui.reference_image_path,
                     sizeof(goxel.gui.reference_image_path), "%s", value);
        }
        if (strcmp(name, "x") == 0) {
            goxel.gui.reference_image_pos[0] = atof(value);
            goxel.gui.reference_image_pos_set = true;
        }
        if (strcmp(name, "y") == 0) {
            goxel.gui.reference_image_pos[1] = atof(value);
            goxel.gui.reference_image_pos_set = true;
        }
        if (strcmp(name, "w") == 0) {
            goxel.gui.reference_image_size[0] = atof(value);
            goxel.gui.reference_image_size_set = true;
        }
        if (strcmp(name, "h") == 0) {
            goxel.gui.reference_image_size[1] = atof(value);
            goxel.gui.reference_image_size_set = true;
        }
        if (strcmp(name, "zoom") == 0) {
            goxel.gui.reference_image_zoom = atof(value);
        }
        if (strcmp(name, "alpha") == 0) {
            goxel.gui.reference_image_alpha = atof(value);
        }
        if (strcmp(name, "pan_x") == 0) {
            goxel.gui.reference_image_pan[0] = atof(value);
        }
        if (strcmp(name, "pan_y") == 0) {
            goxel.gui.reference_image_pan[1] = atof(value);
        }
    }
    if (strcmp(section, "view_cube") == 0) {
        if (strcmp(name, "visible") == 0) {
            goxel.gui.view_cube_visible = atoi(value) != 0;
        }
        if (strcmp(name, "x") == 0) {
            goxel.gui.view_cube_pos[0] = atof(value);
            goxel.gui.view_cube_pos_set = true;
        }
        if (strcmp(name, "y") == 0) {
            goxel.gui.view_cube_pos[1] = atof(value);
            goxel.gui.view_cube_pos_set = true;
        }
        if (strcmp(name, "w") == 0) {
            goxel.gui.view_cube_size[0] = atof(value);
            goxel.gui.view_cube_size_set = true;
        }
        if (strcmp(name, "h") == 0) {
            goxel.gui.view_cube_size[1] = atof(value);
            goxel.gui.view_cube_size_set = true;
        }
    }
    if (strcmp(section, "axis_widget") == 0) {
        if (strcmp(name, "visible") == 0) {
            goxel.gui.axis_widget_visible = atoi(value) != 0;
        }
        if (strcmp(name, "x") == 0) {
            goxel.gui.axis_widget_pos[0] = atof(value);
            goxel.gui.axis_widget_pos_set = true;
        }
        if (strcmp(name, "y") == 0) {
            goxel.gui.axis_widget_pos[1] = atof(value);
            goxel.gui.axis_widget_pos_set = true;
        }
        if (strcmp(name, "w") == 0) {
            goxel.gui.axis_widget_size[0] = atof(value);
            goxel.gui.axis_widget_size_set = true;
        }
        if (strcmp(name, "h") == 0) {
            goxel.gui.axis_widget_size[1] = atof(value);
            goxel.gui.axis_widget_size_set = true;
        }
    }
    return 0;
}

void settings_load(void)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/settings.ini", sys_get_user_dir());
    LOG_I("Read settings file: %s", path);
    arrfree(goxel.keymaps);
    goxel.emulate_three_buttons_mouse = 0;
    goxel.gpu_accel_mode = GPU_ACCEL_AUTO;
    goxel.view_effects = 0;
    goxel.frame_spacing = 8;
    goxel.xcom_gox_repository[0] = '\0';
    goxel.gui.topbar_orientation = GUI_LAYOUT_HORIZONTAL;
    goxel.gui.mytoolsbar_orientation = GUI_LAYOUT_HORIZONTAL;
    goxel.gui.paintbar_orientation = GUI_LAYOUT_HORIZONTAL;
    goxel.gui.selectbar_orientation = GUI_LAYOUT_HORIZONTAL;
    goxel.gui.swatchesbar_orientation = GUI_LAYOUT_HORIZONTAL;
    goxel.gui.leftbar_orientation = GUI_LAYOUT_VERTICAL;
    goxel.gui.selection_mode = MODE_REPLACE;
    goxel.gui.topbar_pos_set = false;
    goxel.gui.mytoolsbar_pos_set = false;
    goxel.gui.paintbar_pos_set = false;
    goxel.gui.selectbar_pos_set = false;
    goxel.gui.swatchesbar_pos_set = false;
    goxel.gui.leftbar_pos_set = false;
    goxel.gui.topbar_folded = false;
    goxel.gui.mytoolsbar_folded = false;
    goxel.gui.paintbar_folded = false;
    goxel.gui.selectbar_folded = false;
    goxel.gui.swatchesbar_folded = false;
    goxel.gui.leftbar_folded = false;
    texture_delete(goxel.gui.reference_image_texture);
    goxel.gui.reference_image_texture = NULL;
    goxel.gui.reference_image_path[0] = '\0';
    goxel.gui.reference_image_pos[0] = 0.0f;
    goxel.gui.reference_image_pos[1] = 0.0f;
    goxel.gui.reference_image_size[0] = 320.0f;
    goxel.gui.reference_image_size[1] = 240.0f;
    goxel.gui.reference_image_pan[0] = 0.0f;
    goxel.gui.reference_image_pan[1] = 0.0f;
    goxel.gui.reference_image_zoom = 1.0f;
    goxel.gui.reference_image_alpha = 0.40f;
    goxel.gui.reference_image_visible = false;
    goxel.gui.reference_image_pos_set = false;
    goxel.gui.reference_image_size_set = false;
    goxel.gui.reference_image_load_failed = false;
    goxel.gui.view_cube_pos[0] = 0.0f;
    goxel.gui.view_cube_pos[1] = 0.0f;
    goxel.gui.view_cube_size[0] = 128.0f;
    goxel.gui.view_cube_size[1] = 128.0f;
    goxel.gui.view_cube_visible = true;
    goxel.gui.view_cube_pos_set = false;
    goxel.gui.view_cube_size_set = false;
    goxel.gui.axis_widget_pos[0] = 0.0f;
    goxel.gui.axis_widget_pos[1] = 0.0f;
    goxel.gui.axis_widget_size[0] = 112.0f;
    goxel.gui.axis_widget_size[1] = 112.0f;
    goxel.gui.axis_widget_visible = true;
    goxel.gui.axis_widget_pos_set = false;
    goxel.gui.axis_widget_size_set = false;
    ini_parse(path, settings_ini_handler, NULL);
    if (!goxel.gui.paintbar_pos_set || !goxel.gui.swatchesbar_pos_set) {
        goxel.gui.topbar_pos_set = false;
        goxel.gui.paintbar_pos_set = false;
        goxel.gui.swatchesbar_pos_set = false;
        goxel.gui.leftbar_pos_set = false;
    }
    actions_check_shortcuts();
    gesture_set_emulate_three_buttons_mouse(goxel.emulate_three_buttons_mouse);
}

static int shortcut_save_callback(action_t *a, void *user)
{
    FILE *file = user;
    if (strcmp(a->shortcut, a->default_shortcut ?: "") != 0)
        fprintf(file, "%s=%s\n", a->id, a->shortcut);
    return 0;
}

static void save_keymaps(FILE *file)
{
    int i, action, input;

    fprintf(file, "[keymaps]\n");
    for (i = 0; i < arrlen(goxel.keymaps); i++) {
        action = goxel.keymaps[i].action;
        input = goxel.keymaps[i].input;
        switch (action) {
        case 0:
            fprintf(file, "pan=");
            break;
        case 1:
            fprintf(file, "rotate=");
            break;
        case 2:
            fprintf(file, "zoom=");
            break;
        default:
            assert(false);
            continue;
        }
        if (input & GESTURE_CTRL) {
            fprintf(file, "ctrl ");
        }
        if (input & GESTURE_SHIFT) {
            fprintf(file, "shift ");
        }
        if (input & GESTURE_MMB) {
            fprintf(file, "middle mouse");
        }
        if (input & GESTURE_RMB) {
            fprintf(file, "right mouse");
        }
        fprintf(file, "\n");
    }
}

void settings_save(void)
{
    char path[1024];
    FILE *file;

    snprintf(path, sizeof(path), "%s/settings.ini", sys_get_user_dir());
    LOG_I("Save settings to %s", path);
    sys_make_dir(path);
    file = fopen(path, "w");
    if (!file) {
        LOG_E("Cannot save settings to %s: %s", path, strerror(errno));
        return;
    }
    fprintf(file, "[ui]\n");
    fprintf(file, "theme=%s\n", theme_get()->name);
    fprintf(file, "language=%s\n", goxel.lang);
    fprintf(file, "scale=%f\n", gui_get_scale());
    fprintf(file, "topbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.topbar_orientation));
    fprintf(file, "mytoolsbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.mytoolsbar_orientation));
    fprintf(file, "paintbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.paintbar_orientation));
    fprintf(file, "selectbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.selectbar_orientation));
    fprintf(file, "swatchesbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.swatchesbar_orientation));
    fprintf(file, "leftbar_orientation=%s\n",
            layout_orientation_to_string(goxel.gui.leftbar_orientation));
    fprintf(file, "topbar_folded=%d\n", goxel.gui.topbar_folded);
    fprintf(file, "mytoolsbar_folded=%d\n", goxel.gui.mytoolsbar_folded);
    fprintf(file, "paintbar_folded=%d\n", goxel.gui.paintbar_folded);
    fprintf(file, "selectbar_folded=%d\n", goxel.gui.selectbar_folded);
    fprintf(file, "swatchesbar_folded=%d\n", goxel.gui.swatchesbar_folded);
    fprintf(file, "leftbar_folded=%d\n", goxel.gui.leftbar_folded);
    if (goxel.gui.topbar_pos_set) {
        fprintf(file, "topbar_x=%f\n", goxel.gui.topbar_pos[0]);
        fprintf(file, "topbar_y=%f\n", goxel.gui.topbar_pos[1]);
    }
    if (goxel.gui.mytoolsbar_pos_set) {
        fprintf(file, "mytoolsbar_x=%f\n", goxel.gui.mytoolsbar_pos[0]);
        fprintf(file, "mytoolsbar_y=%f\n", goxel.gui.mytoolsbar_pos[1]);
    }
    if (goxel.gui.paintbar_pos_set) {
        fprintf(file, "paintbar_x=%f\n", goxel.gui.paintbar_pos[0]);
        fprintf(file, "paintbar_y=%f\n", goxel.gui.paintbar_pos[1]);
    }
    if (goxel.gui.selectbar_pos_set) {
        fprintf(file, "selectbar_x=%f\n", goxel.gui.selectbar_pos[0]);
        fprintf(file, "selectbar_y=%f\n", goxel.gui.selectbar_pos[1]);
    }
    if (goxel.gui.swatchesbar_pos_set) {
        fprintf(file, "swatchesbar_x=%f\n", goxel.gui.swatchesbar_pos[0]);
        fprintf(file, "swatchesbar_y=%f\n", goxel.gui.swatchesbar_pos[1]);
    }
    if (goxel.gui.leftbar_pos_set) {
        fprintf(file, "leftbar_x=%f\n", goxel.gui.leftbar_pos[0]);
        fprintf(file, "leftbar_y=%f\n", goxel.gui.leftbar_pos[1]);
    }
    fprintf(file, "\n");

    fprintf(file, "[xcom]\n");
    fprintf(file, "gox_repository=%s\n", goxel.xcom_gox_repository);
    fprintf(file, "\n");

    fprintf(file, "[gpu_acceleration]\n");
    fprintf(file, "mode=%s\n",
            goxel.gpu_accel_mode == GPU_ACCEL_OFF ? "off" :
            goxel.gpu_accel_mode == GPU_ACCEL_VALIDATION ?
                "validation" : "auto");
    fprintf(file, "\n");

    fprintf(file, "[view]\n");
    fprintf(file, "grid=%d\n", !!(goxel.view_effects & EFFECT_GRID));
    fprintf(file, "edges=%d\n", !!(goxel.view_effects & EFFECT_EDGES));
    fprintf(file, "frames=%d\n", !!(goxel.view_effects & EFFECT_FRAMES));
    fprintf(file, "frame_spacing=%d\n", goxel.frame_spacing);
    fprintf(file, "\n");

    fprintf(file, "[reference]\n");
    fprintf(file, "visible=%d\n", goxel.gui.reference_image_visible);
    fprintf(file, "path=%s\n", goxel.gui.reference_image_path);
    if (goxel.gui.reference_image_pos_set) {
        fprintf(file, "x=%f\n", goxel.gui.reference_image_pos[0]);
        fprintf(file, "y=%f\n", goxel.gui.reference_image_pos[1]);
    }
    if (goxel.gui.reference_image_size_set) {
        fprintf(file, "w=%f\n", goxel.gui.reference_image_size[0]);
        fprintf(file, "h=%f\n", goxel.gui.reference_image_size[1]);
    }
    fprintf(file, "zoom=%f\n", goxel.gui.reference_image_zoom);
    fprintf(file, "alpha=%f\n", goxel.gui.reference_image_alpha);
    fprintf(file, "pan_x=%f\n", goxel.gui.reference_image_pan[0]);
    fprintf(file, "pan_y=%f\n", goxel.gui.reference_image_pan[1]);
    fprintf(file, "\n");

    fprintf(file, "[view_cube]\n");
    fprintf(file, "visible=%d\n", goxel.gui.view_cube_visible);
    if (goxel.gui.view_cube_pos_set) {
        fprintf(file, "x=%f\n", goxel.gui.view_cube_pos[0]);
        fprintf(file, "y=%f\n", goxel.gui.view_cube_pos[1]);
    }
    if (goxel.gui.view_cube_size_set) {
        fprintf(file, "w=%f\n", goxel.gui.view_cube_size[0]);
        fprintf(file, "h=%f\n", goxel.gui.view_cube_size[1]);
    }
    fprintf(file, "\n");

    fprintf(file, "[axis_widget]\n");
    fprintf(file, "visible=%d\n", goxel.gui.axis_widget_visible);
    if (goxel.gui.axis_widget_pos_set) {
        fprintf(file, "x=%f\n", goxel.gui.axis_widget_pos[0]);
        fprintf(file, "y=%f\n", goxel.gui.axis_widget_pos[1]);
    }
    if (goxel.gui.axis_widget_size_set) {
        fprintf(file, "w=%f\n", goxel.gui.axis_widget_size[0]);
        fprintf(file, "h=%f\n", goxel.gui.axis_widget_size[1]);
    }
    fprintf(file, "\n");

    fprintf(file, "[shortcuts]\n");
    actions_iter(shortcut_save_callback, file);

    if (goxel.emulate_three_buttons_mouse) {
        assert(goxel.emulate_three_buttons_mouse == KEY_LEFT_ALT);
        fprintf(file, "[inputs]\n");
        fprintf(file, "emulate_three_buttons_mouse=alt");
    }

    fprintf(file, "\n");
    save_keymaps(file);
    fprintf(file, "\n");

    fclose(file);
}
