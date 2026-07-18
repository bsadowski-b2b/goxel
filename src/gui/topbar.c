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

#include <errno.h>

#ifndef GUI_CUSTOM_TOPBAR

static void a_xcom_save_swatch_palette(void)
{
    char path[1024];
    FILE *file;
    int i;

    snprintf(path, sizeof(path), "%s/palettes/XCom Toolbar Palette.gpl",
             sys_get_user_dir());
    sys_make_dir(path);
    file = fopen(path, "w");
    if (!file) {
        LOG_E("Cannot save palette to %s: %s", path, strerror(errno));
        return;
    }

    fprintf(file, "GIMP Palette\n");
    fprintf(file, "Name: XCom Toolbar Palette\n");
    fprintf(file, "Columns: %d\n", XCOM_SWATCHES_COUNT);
    fprintf(file, "#\n");
    for (i = 0; i < XCOM_SWATCHES_COUNT; i++) {
        fprintf(file, "%3d %3d %3d\tXCom Color %d\n",
                goxel.image->xcom_swatches[i][0],
                goxel.image->xcom_swatches[i][1],
                goxel.image->xcom_swatches[i][2],
                i + 1);
    }
    fclose(file);
    goxel_add_hint(0, NULL, "Saved XCom palette");
}

ACTION_REGISTER(ACTION_xcom_save_swatch_palette,
    .help = N_("Saves toolbar palette"),
    .cfunc = a_xcom_save_swatch_palette,
    .icon = ICON_PALETTE,
)

static void a_xcom_load_swatch_palette(void)
{
    const char *path;
    const char *filters[] = {"*.gpl", "*.dat", "*.png", NULL};
    palette_t palette;
    int i, nb;

    path = sys_open_file_dialog("Load XCom Palette", NULL, filters,
                                "gpl, dat, png");
    if (!path) return;
    if (palette_load_from_file(path, &palette) < 0) {
        LOG_E("Cannot load XCom palette from %s", path);
        return;
    }

    nb = min(palette.size, XCOM_SWATCHES_COUNT);
    for (i = 0; i < nb; i++) {
        memcpy(goxel.image->xcom_swatches[i], palette.entries[i].color, 4);
    }
    if (nb > 0 && memcmp(goxel.painter.color,
                         goxel.image->xcom_swatches[0], 4) != 0)
        memcpy(goxel.painter.color, goxel.image->xcom_swatches[0], 4);
    palette_clear(&palette);
    goxel_add_hint(0, NULL, "Loaded XCom palette");
}

ACTION_REGISTER(ACTION_xcom_load_swatch_palette,
    .help = N_("Loads toolbar palette"),
    .cfunc = a_xcom_load_swatch_palette,
    .icon = ICON_PALETTE,
)

static void gui_topbar_actions(int orientation)
{
    int i;
    static const int ACTIONS[] = {
        ACTION_undo,
        ACTION_redo,
        ACTION_layer_clear,
        ACTION_view_default,
        ACTION_view_toggle_grid_edges,
    };

    gui_group_begin(NULL);
    if (orientation == GUI_LAYOUT_HORIZONTAL)
        gui_row_begin(0);
    for (i = 0; i < ARRAY_SIZE(ACTIONS); i++) {
        gui_action_button(ACTIONS[i], NULL, 0);
    }
    if (orientation == GUI_LAYOUT_HORIZONTAL)
        gui_row_end();
    gui_group_end();
}

static int gui_mode_select(int orientation)
{
    bool v;
    char label[64];
    const action_t *action = NULL;
    int i;
    const struct {
        int mode;
        const char *label;
        int action;
        int icon;
    } values[] = {
        {MODE_OVER,     _("Add"),     ACTION_set_mode_add,    ICON_MODE_ADD},
        {MODE_SUB,      _("Sub"),     ACTION_set_mode_sub,    ICON_MODE_SUB},
        {MODE_PAINT,    _("Paint"),   ACTION_set_mode_paint,  ICON_MODE_PAINT},
    };
    // XXX: almost the same as in tools_panel.
    gui_group_begin(NULL);
    if (orientation == GUI_LAYOUT_HORIZONTAL)
        gui_row_begin(0);
    for (i = 0; i < ARRAY_SIZE(values); i++) {
        v = goxel.painter.mode == values[i].mode;
        action = action_get(values[i].action, true);
        sprintf(label, "%s (%s)", values[i].label, action->shortcut);
        if (gui_selectable_icon(label, &v, values[i].icon)) {
            action_exec(action);
        }
    }
    if (orientation == GUI_LAYOUT_HORIZONTAL)
        gui_row_end();
    gui_group_end();
    return 0;
}

static void gui_xcom_swatches(const char *id_prefix, int per_row)
{
    int i;
    char label[64];

    for (i = 0; i < XCOM_SWATCHES_COUNT; i++) {
        if (per_row && i % per_row == 0)
            gui_row_begin(0);
        snprintf(label, sizeof(label), "##%s_%d", id_prefix, i);
        gui_color_swatch(label, goxel.image->xcom_swatches[i],
                         goxel.painter.color);
        if (per_row && (i % per_row == per_row - 1 ||
                        i == XCOM_SWATCHES_COUNT - 1))
            gui_row_end();
    }
}

void gui_xcom_panel(void)
{
    gui_request_panel_width(340);

    if (gui_section_begin(_("Palette"), false)) {
        gui_xcom_swatches("xcom_panel_color", XCOM_SWATCHES_COUNT);
    } gui_section_end();

    if (gui_section_begin(_("Save"), GUI_SECTION_COLLAPSABLE)) {
        gui_action_button(ACTION_save, _("Save .gox"), 1.0);
        gui_action_button(ACTION_save_as, _("Save .gox As"), 1.0);
        gui_action_button(ACTION_xcom_save_swatch_palette,
                          _("Save Palette"), 1.0);
        gui_action_button(ACTION_xcom_load_swatch_palette,
                          _("Load Palette"), 1.0);
    } gui_section_end();
}

void gui_top_bar(int orientation)
{
    if (orientation == GUI_LAYOUT_HORIZONTAL) {
        gui_row_begin(0); {
        gui_toolbar_handle("Drag top toolbar");
        gui_topbar_actions(orientation);
        gui_row_begin(0); {
            gui_mode_select(orientation);
            gui_color("##color", goxel.painter.color);
            gui_xcom_swatches("xcom_color", 0);
        } gui_row_end();
        } gui_row_end();
    } else {
        gui_toolbar_handle("Drag top toolbar");
        gui_topbar_actions(orientation);
        gui_mode_select(orientation);
        gui_color("##color", goxel.painter.color);
        gui_xcom_swatches("xcom_color", 0);
    }
}

#endif // GUI_CUSTOM_TOPBAR
