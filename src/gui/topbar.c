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

    snprintf(path, sizeof(path), "%s/palettes/XCom Toolbar Swatches.gpl",
             sys_get_user_dir());
    sys_make_dir(path);
    file = fopen(path, "w");
    if (!file) {
        LOG_E("Cannot save palette to %s: %s", path, strerror(errno));
        return;
    }

    fprintf(file, "GIMP Palette\n");
    fprintf(file, "Name: XCom Toolbar Swatches\n");
    fprintf(file, "Columns: %d\n", XCOM_SWATCHES_COUNT);
    fprintf(file, "#\n");
    for (i = 0; i < XCOM_SWATCHES_COUNT; i++) {
        fprintf(file, "%3d %3d %3d\tXCom Swatch %d\n",
                goxel.image->xcom_swatches[i][0],
                goxel.image->xcom_swatches[i][1],
                goxel.image->xcom_swatches[i][2],
                i + 1);
    }
    fclose(file);
    goxel_add_hint(0, NULL, "Saved XCom swatches palette");
}

ACTION_REGISTER(ACTION_xcom_save_swatch_palette,
    .help = N_("Saves toolbar swatches as a palette"),
    .cfunc = a_xcom_save_swatch_palette,
    .icon = ICON_PALETTE,
)

static void gui_topbar_actions(void)
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
    gui_row_begin(0);
    for (i = 0; i < ARRAY_SIZE(ACTIONS); i++) {
        gui_action_button(ACTIONS[i], NULL, 0);
    }
    gui_row_end();
    gui_group_end();
}

static int gui_mode_select(void)
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
    gui_row_begin(0);
    for (i = 0; i < ARRAY_SIZE(values); i++) {
        v = goxel.painter.mode == values[i].mode;
        action = action_get(values[i].action, true);
        sprintf(label, "%s (%s)", values[i].label, action->shortcut);
        if (gui_selectable_icon(label, &v, values[i].icon)) {
            action_exec(action);
        }
    }
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
    gui_request_panel_width(260);

    if (gui_section_begin(_("Swatches"), false)) {
        gui_color_small(_("Active"), goxel.painter.color);
        gui_xcom_swatches("xcom_panel_swatch", 4);
        gui_section_end();
    }

    if (gui_section_begin(_("Save"), false)) {
        gui_action_button(ACTION_save, _("Save .gox"), 1.0);
        gui_action_button(ACTION_save_as, _("Save .gox As"), 1.0);
        gui_action_button(ACTION_xcom_save_swatch_palette,
                          _("Save Palette"), 1.0);
        gui_section_end();
    }
}

void gui_top_bar(void)
{
    gui_row_begin(0); {
        gui_topbar_actions();
        gui_row_begin(0); {
            gui_mode_select();
            gui_color("##color", goxel.painter.color);
            gui_xcom_swatches("xcom_swatch", 0);
        } gui_row_end();
    } gui_row_end();
}

#endif // GUI_CUSTOM_TOPBAR
