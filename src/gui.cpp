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

#ifndef GUI_HAS_SCROLLBARS
#   define GUI_HAS_SCROLLBARS 1
#endif

extern "C" {
#include "goxel.h"
#include "utils/color.h"

void gui_app(void);
void gui_render_panel(void);
bool gui_pan_scroll_behavior(int dir);
}

#ifndef typeof
#   define typeof __typeof__
#endif

#define IM_VEC4_CLASS_EXTRA \
        ImVec4(const uint8_t f[4]) { \
            x = f[0] / 255.; \
            y = f[1] / 255.; \
            z = f[2] / 255.; \
            w = f[3] / 255.; }     \
        ImVec4(const float f[4]) { \
            x = f[0]; \
            y = f[1]; \
            z = f[2]; \
            w = f[3]; }     \


// Prevent warnings with gcc.
#ifndef __clang__
#pragma GCC diagnostic push
#if __GNUC__ >= 8
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
// #define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

#include "../ext_src/imgui/imgui.h"
#include "../ext_src/imgui/imgui_internal.h"
#include "../ext_src/imgui/ImGuizmo.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifndef __clang__
#pragma GCC diagnostic pop
#endif

// How much space we keep for the labels on the left.
static const float LABEL_SIZE = 90;

#ifndef GUI_ITEM_HEIGHT
#   define GUI_ITEM_HEIGHT 18
#endif

#ifndef GUI_ICON_HEIGHT
#   define GUI_ICON_HEIGHT 32
#endif

static const float COLOR_SWATCH_SCALE = 2.0f / 3.0f;
static const ImVec2 ITEM_SPACING = ImVec2(8, 4);

static ImVec2 color_swatch_size(void)
{
    const float size = GUI_ICON_HEIGHT * COLOR_SWATCH_SCALE;
    return ImVec2(size, size);
}

static ImVec2 color_small_swatch_size(void)
{
    const float size = gui_get_item_height() * COLOR_SWATCH_SCALE;
    return ImVec2(size, size);
}

#define COL_HEX(x) ImVec4( \
        ((uint8_t)((x >> 24) & 0xff)) / 255.0, \
        ((uint8_t)((x >> 16) & 0xff)) / 255.0, \
        ((uint8_t)((x >> 8) & 0xff)) / 255.0, \
        ((uint8_t)((x >> 0) & 0xff)) / 255.0)

static inline ImVec4 color_lighten(ImVec4 c, float k = 0.1)
{
    float h, s, v, r, g, b;
    r = c.x;
    g = c.y;
    b = c.z;
    ImGui::ColorConvertRGBtoHSV(r, g, b, h, s, v);
    v += k;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImVec4(r, g, b, c.w);
}

static inline ImVec4 color_lighten2(ImVec4 v)
{
    return color_lighten(v, 0.2);
}

static inline ImVec4 color_with_alpha(ImVec4 v, float alpha)
{
    v.w = alpha;
    return v;
}

static texture_t *g_tex_icons = NULL;

static const char *VSHADER =
    "                                                               \n"
    "attribute vec3 a_pos;                                          \n"
    "attribute vec2 a_tex_pos;                                      \n"
    "attribute vec4 a_color;                                        \n"
    "                                                               \n"
    "uniform mat4 u_proj_mat;                                       \n"
    "                                                               \n"
    "varying vec2 v_tex_pos;                                        \n"
    "varying vec4 v_color;                                          \n"
    "                                                               \n"
    "void main()                                                    \n"
    "{                                                              \n"
    "    gl_Position = u_proj_mat * vec4(a_pos, 1.0);               \n"
    "    v_tex_pos = a_tex_pos;                                     \n"
    "    v_color = a_color;                                         \n"
    "}                                                              \n"
;

static const char *FSHADER =
    "                                                               \n"
    "#ifdef GL_ES                                                   \n"
    "precision mediump float;                                       \n"
    "#endif                                                         \n"
    "                                                               \n"
    "uniform sampler2D u_tex;                                       \n"
    "                                                               \n"
    "varying vec2 v_tex_pos;                                        \n"
    "varying vec4 v_color;                                          \n"
    "                                                               \n"
    "void main()                                                    \n"
    "{                                                              \n"
    "    gl_FragColor = v_color * texture2D(u_tex, v_tex_pos);      \n"
    "}                                                              \n"
;

enum {
    A_POS_LOC = 0,
    A_TEX_POS_LOC,
    A_COLOR_LOC,
};

static const char *ATTR_NAMES[] = {
    "a_pos",
    "a_tex_pos",
    "a_color",
    NULL
};

typedef typeof(((inputs_t*)0)->safe_margins) margins_t;

typedef struct gui_t {
    bool initialized;

    gl_shader_t *shader;
    GLuint  array_buffer;
    GLuint  index_buffer;
    margins_t margins;

    // bitmask: 1 - some window is scrolling.
    //          2 - some window was scrolling last frame.
    int     scrolling;

    int     can_move_window;
    bool    want_capture_mouse;

    int     is_row;
    float   item_size;

    bool    is_context_menu;
    int     context_menu_row;

    int     win_dir; // Store the current window direction (for scrolling).
    int     win_style_color_count;
    int     toolbar_depth;

    struct {
        const char *title;
        int       (*func)(void *data);
        void      (*on_closed)(int);
        int         flags;
        void       *data; // Automatically released when popup close.
        bool        opened;
    } popup[8]; // Stack of modal popups
    int popup_count;

    bool item_activated;
    bool item_deactivated;

    // Stack of the groups.
    int groups_count;
    struct {
        bool activated;
        bool deactivated;
    } groups[16];

    float scale;

} gui_t;

static gui_t *gui = NULL;

static void align_toolbar_item(ImVec2 size)
{
    ImVec2 pos;

    if (!gui || !gui->toolbar_depth) return;
    pos = ImGui::GetCursorPos();
    if (gui->is_row && size.y < GUI_ICON_HEIGHT)
        pos.y += (GUI_ICON_HEIGHT - size.y) / 2.0f;
    if (!gui->is_row && size.x < GUI_ICON_HEIGHT)
        pos.x += (GUI_ICON_HEIGHT - size.x) / 2.0f;
    ImGui::SetCursorPos(pos);
}

static void gui_create(void)
{
    if (gui) return;
    gui = (gui_t*)calloc(1, sizeof(*gui));
    gui->scale = 1;
}

float gui_get_scale(void)
{
    gui_create();
    return gui->scale;
}

void gui_set_scale(float s)
{
    if (s < 1 || s > 2) {
        LOG_W("Invalid scale value: %f", s);
        s = 1;
    }
    gui_create();
    gui->scale = s;

    if (gui->initialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexID = 0; // Note: this leaks the texture.
    }
}

static void on_click(void) {
    if (DEFINED(GUI_SOUND))
        sound_play("click", 1.0, 1.0);
}

static bool isCharPressed(int c)
{
    // TODO: remove this function if possible.
    ImGuiContext& g = *GImGui;
    if (g.IO.InputQueueCharacters.Size == 0) return false;
    return g.IO.InputQueueCharacters[0] == c;
}

static bool parse_mouse_shortcut(const char *s, int *button)
{
    char *end = NULL;
    long id;

    if (!str_startswith(s, "Mouse")) return false;
    id = strtol(s + strlen("Mouse"), &end, 10);
    if (!end || *end != '\0') return false;
    if (id < 1 || id > INPUT_MOUSE_BUTTON_COUNT) return false;
    *button = id - 1;
    return true;
}

#define COLOR(g, c, s) ({ \
        uint8_t c_[4]; \
        theme_get_color(THEME_GROUP_##g, THEME_COLOR_##c, (s), c_); \
        ImVec4 ret_ = c_; ret_; })

/*
 * Return the color that should be used to draw an icon depending on the
 * style and the icon.  Some icons shouldn't have their color change with
 * the style and some other do.
 */
static uint32_t get_icon_color(int icon, bool selected)
{
    int group;
    uint8_t color[4];

    group = icon >> 16;
    if (group == 0)
        return ImGui::GetColorU32(COLOR(ICON, TEXT, selected));
    if (group == THEME_GROUP_ICON)
        return 0xFFFFFFFF;
    theme_get_color(group, THEME_COLOR_ITEM, false, color);
    return ImGui::GetColorU32(color);
}

static ImVec2 get_icon_uv(int icon)
{
    icon = icon & 0xffff; // Remove the theme group part.
    return ImVec2(((icon - 1) % 8) / 8.0, ((icon - 1) / 8) / 8.0);
}

static void render_prepare_context(void)
{
    #define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled
    GL(glEnable(GL_BLEND));
    GL(glBlendEquation(GL_FUNC_ADD));
    GL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    GL(glDisable(GL_CULL_FACE));
    GL(glDisable(GL_DEPTH_TEST));
    GL(glEnable(GL_SCISSOR_TEST));
    GL(glActiveTexture(GL_TEXTURE0));

    // Setup orthographic projection matrix
    const float width = ImGui::GetIO().DisplaySize.x;
    const float height = ImGui::GetIO().DisplaySize.y;
    const float ortho_projection[4][4] =
    {
        { 2.0f/width,	0.0f,			0.0f,		0.0f },
        { 0.0f,			2.0f/-height,	0.0f,		0.0f },
        { 0.0f,			0.0f,			-1.0f,		0.0f },
        { -1.0f,		1.0f,			0.0f,		1.0f },
    };
    GL(glUseProgram(gui->shader->prog));
    gl_update_uniform(gui->shader, "u_tex", 0);
    gl_update_uniform(gui->shader, "u_proj_mat", ortho_projection);

    GL(glBindBuffer(GL_ARRAY_BUFFER, gui->array_buffer));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gui->index_buffer));
    // This could probably be done only at init time.
    GL(glEnableVertexAttribArray(A_POS_LOC));
    GL(glEnableVertexAttribArray(A_TEX_POS_LOC));
    GL(glEnableVertexAttribArray(A_COLOR_LOC));
    GL(glVertexAttribPointer(A_POS_LOC, 2, GL_FLOAT, false,
                             sizeof(ImDrawVert),
                             (void*)OFFSETOF(ImDrawVert, pos)));
    GL(glVertexAttribPointer(A_TEX_POS_LOC, 2, GL_FLOAT, false,
                             sizeof(ImDrawVert),
                             (void*)OFFSETOF(ImDrawVert, uv)));
    GL(glVertexAttribPointer(A_COLOR_LOC, 4, GL_UNSIGNED_BYTE,
                             true, sizeof(ImDrawVert),
                             (void*)OFFSETOF(ImDrawVert, col)));
    #undef OFFSETOF
}

static void ImImpl_RenderDrawLists(ImDrawData* draw_data)
{
    const float height = ImGui::GetIO().DisplaySize.y;
    const float scale = ImGui::GetIO().DisplayFramebufferScale.y;
    render_prepare_context();
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        if (cmd_list->VtxBuffer.size())
            GL(glBufferData(GL_ARRAY_BUFFER,
                    (GLsizeiptr)cmd_list->VtxBuffer.size() * sizeof(ImDrawVert),
                    (GLvoid*)&cmd_list->VtxBuffer.front(), GL_DYNAMIC_DRAW));

        if (cmd_list->IdxBuffer.size())
            GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    (GLsizeiptr)cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx),
                    (GLvoid*)&cmd_list->IdxBuffer.front(), GL_DYNAMIC_DRAW));

        for (const ImDrawCmd* pcmd = cmd_list->CmdBuffer.begin(); pcmd != cmd_list->CmdBuffer.end(); pcmd++)
        {
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
                render_prepare_context(); // Restore context.
            }
            else
            {
                GL(glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID()));
                GL(glScissor((int)pcmd->ClipRect.x * scale,
                             (int)(height - pcmd->ClipRect.w) * scale,
                             (int)(pcmd->ClipRect.z - pcmd->ClipRect.x) * scale,
                             (int)(pcmd->ClipRect.w - pcmd->ClipRect.y) * scale));
                GL(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
                                  GL_UNSIGNED_SHORT,
                                  (void*)(uintptr_t)(pcmd->IdxOffset * 2)));
            }
        }
    }
    GL(glDisable(GL_SCISSOR_TEST));
}

static void add_font(const char *uri, const ImWchar *ranges, bool mergmode)
{
    ImGuiIO& io = ImGui::GetIO();
    float scale = goxel.screen_scale;
    const void *data;
    int data_size;
    ImFontConfig conf;
    float size = 14 * gui->scale * scale;

    conf.FontDataOwnedByAtlas = false;
    conf.MergeMode = mergmode;
    data = assets_get(uri, &data_size);
    assert(data);
    io.Fonts->AddFontFromMemoryTTF(
            (void*)data, data_size, size, &conf, ranges);
}

static void load_fonts_texture()
{
    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels;
    int width, height;
    const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x25A0, 0x25FF, // Geometric shapes
        0
    };
    const ImWchar range_user[] = {
        0xE660, 0xE6FF,
        0
    };

    io.Fonts->Clear();
    add_font("asset://data/fonts/DejaVuSans.ttf", ranges, false);
    add_font("asset://data/fonts/goxel-font.ttf", range_user, true);

    #if 0
    conf.MergeMode = true;
    data = assets_get(
            "asset://data/fonts/DroidSansFallbackFull.ttf", &data_size);
    assert(data);
    io.Fonts->AddFontFromMemoryTTF(
            (void*)data, data_size, 14 * scale, &conf,
            io.Fonts->GetGlyphRangesJapanese());
    #endif
    io.Fonts->Build();

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    GLuint tex_id;
    GL(glGenTextures(1, &tex_id));
    GL(glActiveTexture(GL_TEXTURE0));
    GL(glBindTexture(GL_TEXTURE_2D, tex_id));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    io.Fonts->TexID = (intptr_t)tex_id;
}

static void init_ImGui(void)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f/60.0f;
    io.IniFilename = NULL;

    io.KeyMap[ImGuiKey_Tab]         = KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow]   = KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow]  = KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow]     = KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow]   = KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp]      = KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown]    = KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home]        = KEY_HOME;
    io.KeyMap[ImGuiKey_End]         = KEY_END;
    io.KeyMap[ImGuiKey_Delete]      = KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace]   = KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Enter]       = KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape]      = KEY_ESCAPE;
    io.KeyMap[ImGuiKey_Space]       = ' ';
    io.KeyMap[ImGuiKey_A]           = 'A';
    io.KeyMap[ImGuiKey_C]           = 'C';
    io.KeyMap[ImGuiKey_V]           = 'V';
    io.KeyMap[ImGuiKey_X]           = 'X';
    io.KeyMap[ImGuiKey_Y]           = 'Y';
    io.KeyMap[ImGuiKey_Z]           = 'Z';

    if (DEFINED(__linux__)) {
        io.SetClipboardTextFn = sys_set_clipboard_text;
        io.GetClipboardTextFn = sys_get_clipboard_text;
    }
}


static void gui_init(void)
{
    gui_create();
    if (!gui->initialized) {
        init_ImGui();
        goxel.gui.panel_width = GUI_PANEL_WIDTH_NORMAL;
        gui->initialized = true;
    }

    if (!gui->shader) {
        gui->shader = gl_shader_create(VSHADER, FSHADER, NULL, ATTR_NAMES);
        GL(glGenBuffers(1, &gui->array_buffer));
        GL(glGenBuffers(1, &gui->index_buffer));
    }

    if (!g_tex_icons) {
        g_tex_icons = texture_new_image("asset://data/images/icons.png", 0);
        GL(glBindTexture(GL_TEXTURE_2D, g_tex_icons->tex));
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->TexID) load_fonts_texture();
}

void gui_release(void)
{
    if (gui) ImGui::DestroyContext();
}

void gui_release_graphics(void)
{
    ImGuiIO& io = ImGui::GetIO();
    gl_shader_delete(gui->shader);
    gui->shader = NULL;
    GL(glDeleteBuffers(1, &gui->array_buffer));
    GL(glDeleteBuffers(1, &gui->index_buffer));
    texture_delete(g_tex_icons);
    g_tex_icons = NULL;

    GL(glDeleteTextures(1, (GLuint*)&io.Fonts->TexID));
    io.Fonts->TexID = 0;
    io.Fonts->Clear();
}

static int alert_popup(void *data)
{
    if (data) gui_text((const char *)data);
    return gui_button("OK", 0, 0);
}

static int check_action_shortcut(action_t *action, void *user)
{
    ImGuiIO& io = ImGui::GetIO();
    const inputs_t *inputs = (const inputs_t*)user;
    const char *s = action->shortcut;
    bool check_key = true;
    bool check_char = true;
    int mouse_button;
    if (!*s) return 0;

    if (io.KeyCtrl) {
        if (!str_startswith(s, "Ctrl")) return 0;
        s += strlen("Ctrl ");
        check_char = false;
    }

    if (io.KeyShift) {
        if (!str_startswith(s, "Shift")) return 0;
        s += strlen("Shift ");
        check_char = false;
    }

    if (str_startswith(s, "Ctrl")) return 0;
    if (str_startswith(s, "Shift")) return 0;

    if (parse_mouse_shortcut(s, &mouse_button)) {
        if (inputs && !io.WantCaptureMouse &&
                inputs->mouse_pressed[mouse_button]) {
            action_exec(action);
            return 1;
        }
        return 0;
    }

    if (    (check_char && isCharPressed(s[0])) ||
            (check_key && ImGui::IsKeyPressed((ImGuiKey)s[0], false))) {
        action_exec(action);
        return 1;
    }
    return 0;
}

static void render_popups(int index)
{
    int r;
    int flags;
    typeof(gui->popup[0]) *popup;
    ImGuiIO& io = ImGui::GetIO();

    popup = &gui->popup[index];
    if (!popup->title) return;

    if (!popup->opened) {
        ImGui::OpenPopup(popup->title);
        popup->opened = true;
    }
    flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    if (popup->flags & GUI_POPUP_FULL) {
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 40,
                                        io.DisplaySize.y - 40),
                (popup->flags & GUI_POPUP_RESIZE) ?  ImGuiCond_Once : 0);
    }
    if (popup->flags & GUI_POPUP_RESIZE) {
        flags &= ~(ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_AlwaysAutoResize);
    }
    if (popup->title[0] == '#') {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(WINDOW, BACKGROUND, false));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, COLOR(WINDOW, INNER, false));

    if (ImGui::BeginPopupModal(popup->title, NULL, flags)) {
        typeof(popup->func) func = popup->func;
        if ((r = func(popup->data))) {
            ImGui::CloseCurrentPopup();
            gui->popup_count--;
            popup->title = NULL;
            popup->func = NULL;
            free(popup->data);
            popup->data = NULL;
            if (popup->on_closed) popup->on_closed(r);
            popup->on_closed = NULL;
            popup->opened = false;
        }
        render_popups(index + 1);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

bool gui_view_cube(float x, float y, float w, float h)
{
    ImGuiIO& io = ImGui::GetIO();
    bool ret = false;
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    camera_t *camera = goxel.image->active_camera;
    float view[4][4];
    float view_prev[4][4];
    const float *projection= (float*)camera->proj_mat;
    const float zup2yup[4][4] = {
        {1, 0, 0, 0},
        {0, 0, -1, 0},
        {0, 1, 0, 0},
        {0, 0, 0, 1},
    };
    const float yup2zup[4][4] = {
        {1, 0, 0, 0},
        {0, 0, 1, 0},
        {0, -1, 0, 0},
        {0, 0, 0, 1},
    };
    ImGuizmo::Style &style = ImGuizmo::GetStyle();

    style.Colors[ImGuizmo::DIRECTION_X] = ImVec4(0.666f, 0.000f, 0.000f, 1.000f);
    style.Colors[ImGuizmo::DIRECTION_Z] = ImVec4(0.000f, 0.666f, 0.000f, 1.000f);
    style.Colors[ImGuizmo::DIRECTION_Y] = ImVec4(0.000f, 0.000f, 0.666f, 1.000f);

    // XXX: ImGuizmo is using Y up.
    mat4_mul(zup2yup, camera->mat, view);
    mat4_invert(view, view);
    mat4_copy(view, view_prev);

    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("Gizmo", NULL, ImGuiWindowFlags_NoDecoration);
    ImGuizmo::SetDrawlist();

    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, w, h);
    ImGuizmo::ViewManipulate(
           (float*)view, projection,
           ImGuizmo::ROTATE, ImGuizmo::LOCAL,
           (float*)&mat4_identity, camera->dist,
           ImGui::GetWindowPos(),
           ImVec2(w, h), 0x0);

    ret = memcmp(view, view_prev, sizeof(view_prev)) != 0;
    if (ret) {
        mat4_invert(view, view);
        mat4_mul(yup2zup, view, camera->mat);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    return ret;
}

static void gui_iter(const inputs_t *inputs)
{
    gui_init();
    unsigned int i;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    io.DisplaySize = ImVec2((float)goxel.screen_size[0],
                            (float)goxel.screen_size[1]);

    io.DisplayFramebufferScale = ImVec2(goxel.screen_scale,
                                        goxel.screen_scale);
    io.DeltaTime = fmax(goxel.delta_time, 0.01);
    io.ConfigDragClickToInputText = true;

    if (inputs) {
        io.DisplayFramebufferScale = ImVec2(inputs->scale, inputs->scale);
        io.FontGlobalScale = 1 / inputs->scale;
        io.MousePos.x = inputs->touches[0].pos[0];
        io.MousePos.y = inputs->touches[0].pos[1];
        io.MouseDown[0] = inputs->touches[0].down[0];
        io.MouseDown[1] = inputs->touches[0].down[1];
        io.MouseDown[2] = inputs->touches[0].down[2];
        for (i = 3; i < ARRAY_SIZE(inputs->mouse_buttons) &&
                    i < (unsigned int)IM_ARRAYSIZE(io.MouseDown); i++)
            io.MouseDown[i] = inputs->mouse_buttons[i];
        gui->margins = inputs->safe_margins;
        io.MouseWheel = inputs->mouse_wheel;

        for (i = 0; i < ARRAY_SIZE(inputs->keys); i++)
            io.KeysDown[i] = inputs->keys[i];
        io.KeyShift = inputs->keys[KEY_LEFT_SHIFT] ||
                      inputs->keys[KEY_RIGHT_SHIFT];
        io.KeyCtrl = inputs->keys[KEY_LEFT_CONTROL] ||
                     inputs->keys[KEY_RIGHT_CONTROL];
        for (i = 0; i < ARRAY_SIZE(inputs->chars); i++) {
            if (!inputs->chars[i]) break;
            io.AddInputCharacter(inputs->chars[i]);
        }
    }


    // Setup theme.
    ImGui::StyleColorsDark();
    style.WindowBorderSize = 0;
    style.WindowPadding = ImVec2(8, 5);
    style.FrameRounding = 2;
    style.ChildRounding = 4;
    style.WindowRounding = 6;
    style.ChildBorderSize = 0;
    style.SelectableTextAlign = ImVec2(0.5, 0.5);
    style.FramePadding = ImVec2(4,
            (GUI_ITEM_HEIGHT * gui->scale - ImGui::GetFontSize()) / 2);
    style.Colors[ImGuiCol_WindowBg] = COLOR(WINDOW, BACKGROUND, false);
    style.Colors[ImGuiCol_ChildBg] = COLOR(SECTION, BACKGROUND, false);
    style.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Text] = COLOR(BASE, TEXT, false);
    style.Colors[ImGuiCol_MenuBarBg] = COLOR(MENU, BACKGROUND, false);

    gui->scrolling = (gui->scrolling & 1) ? 2 : 0;
    gui->can_move_window = (gui->can_move_window & 1) ? 2 : 0;

    // Old code, to remove.
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    gui_app();
    render_popups(0);

    // Handle the shortcuts.  XXX: this should be done with actions.
    if (ImGui::IsKeyPressed((ImGuiKey)KEY_DELETE, false))
        action_exec2(ACTION_layer_clear);

    if (!io.WantCaptureKeyboard) {
        float last_tool_radius = goxel.tool_radius;
        if (isCharPressed('[')) goxel.tool_radius -= 0.5;
        if (isCharPressed(']')) goxel.tool_radius += 0.5;
        if (goxel.tool_radius != last_tool_radius) {
            goxel.tool_radius = clamp(goxel.tool_radius, 0.5, 64);
        }
        actions_iter(check_action_shortcut, (void*)inputs);
    }
    ImGui::EndFrame();
    gui->want_capture_mouse = io.WantCaptureMouse;

    sys_show_keyboard(io.WantTextInput);
}

void gui_render(const inputs_t *inputs)
{
    gui_init();
    gui_iter(inputs);

    ImGui::Render();
    ImImpl_RenderDrawLists(ImGui::GetDrawData());
}

void gui_group_begin(const char *label)
{
    if (label && label[0] != '#') ImGui::Text("%s", label);
    ImGui::PushID(label ?: "group");
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

    memset(&gui->groups[gui->groups_count++], 0, sizeof(gui->groups[0]));
}

void gui_group_end(void)
{
    ImGui::PopID();
    ImGui::PopStyleVar(1);
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::EndGroup();
    if (gui->is_row) ImGui::SameLine();

    // Make sure gui_is_item_activated works at group level.
    assert(gui->groups_count > 0);
    if (gui->groups[gui->groups_count - 1].activated)
        gui->item_activated = true;
    if (gui->groups[gui->groups_count - 1].deactivated)
        gui->item_deactivated = true;
    gui->groups_count--;
}

bool gui_section_begin(const char *label, int flags)
{
    ImGuiChildFlags childflags =
        ImGuiChildFlags_AutoResizeY |
        ImGuiChildFlags_AlwaysUseWindowPadding;
    float padding, w;

    // We ensure that everything stays aligned with widgets outside a section.
    padding = ImGui::GetStyle().WindowPadding.x;
    w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(padding / 2, padding / 2));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - padding / 2);
    ImGui::BeginChild(label, ImVec2(w + padding, 0), childflags);

    if (flags & (GUI_SECTION_COLLAPSABLE | GUI_SECTION_COLLAPSABLE_CLOSED)) {
        ImGui::SetNextItemOpen(
                !(flags & GUI_SECTION_COLLAPSABLE_CLOSED), ImGuiCond_Once);
        return ImGui::CollapsingHeader(label);
    } else {
        if (label && label[0] != '#')
            ImGui::Text("%s", label);
        return true;
    }
}

void gui_section_end(void)
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void gui_row_begin(int nb)
{
    float spacing;
    float avail;
    ImGui::BeginGroup();
    gui->is_row++;
    gui->item_size = 0;
    if (nb) {
        spacing = ImGui::GetStyle().ItemSpacing.x;
        avail = ImGui::GetContentRegionAvail().x;
        gui->item_size = (avail - (nb - 1) * spacing) / nb;
    }
}

void gui_row_end(void)
{
    ImGui::EndGroup();
    gui->is_row--;
    gui->item_size = 0;
}

int gui_window_begin(const char *label, float x, float y, float w, float h,
                     int flags)
{
    ImGuiWindowFlags win_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoDecoration;
    float max_size;
    ImGuiStorage *storage = ImGui::GetStateStorage();
    ImGuiID key;
    float *last_pos;
    int ret = 0;
    int dir = (flags & GUI_WINDOW_HORIZONTAL) ? 0 : 1;

    ImGui::PushID(label);
    if (!gui->can_move_window)
        win_flags |= ImGuiWindowFlags_NoMove;
    if (gui->scrolling)
        win_flags |= ImGuiWindowFlags_NoMouseInputs;
    if (dir == 0)
        win_flags |= ImGuiWindowFlags_HorizontalScrollbar;
    ImGui::SetNextWindowPos(ImVec2(x, y),
            (flags & GUI_WINDOW_MOVABLE) ?
            ImGuiCond_Appearing : ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h));

    key = ImGui::GetID("last_pos");
    last_pos = storage->GetFloatRef(key, dir == 0 ? x : y);

    if ((w == 0) && (dir == 0)) {
        max_size = ImGui::GetMainViewport()->Size.x - *last_pos;
        ImGui::SetNextWindowSizeConstraints(
                ImVec2(0, 0), ImVec2(max_size, FLT_MAX));
    }
    if ((h == 0) && (dir == 1)) {
        max_size = ImGui::GetMainViewport()->Size.y - *last_pos;
        ImGui::SetNextWindowSizeConstraints(
                ImVec2(0, 0), ImVec2(FLT_MAX, max_size));
    }

    assert(gui->win_style_color_count == 0);
    if (flags & GUI_WINDOW_TRANSLUCENT_BACKGROUND) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                color_with_alpha(COLOR(WINDOW, BACKGROUND, false), 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                color_with_alpha(COLOR(SECTION, BACKGROUND, false), 0.70f));
        gui->win_style_color_count = 2;
    }

    ImGui::Begin(label, NULL, win_flags);

    if (flags & GUI_WINDOW_MOVABLE) {
         if (ImGui::GetWindowPos() != ImVec2(x, y))
             ret |= GUI_WINDOW_MOVED;
        *last_pos = dir == 0 ? ImGui::GetWindowPos().x :
                               ImGui::GetWindowPos().y;
    }

    gui->win_dir = dir;
    ImGui::BeginGroup();
    return ret;
}

gui_window_ret_t gui_window_end(void)
{
    gui_window_ret_t ret = {};
    ImGui::EndGroup();
    if (!GUI_HAS_SCROLLBARS && !gui->can_move_window) {
        if (gui_pan_scroll_behavior(gui->win_dir))
            gui->scrolling |= 1;
    }
    ret.x = ImGui::GetWindowPos().x;
    ret.y = ImGui::GetWindowPos().y;
    ret.h = ImGui::GetWindowHeight();
    ret.w = ImGui::GetWindowWidth();
    ImGui::End();
    if (gui->win_style_color_count) {
        ImGui::PopStyleColor(gui->win_style_color_count);
        gui->win_style_color_count = 0;
    }
    ImGui::PopID();

    return ret;
}

bool gui_input_int(const char *label, int *v, int minv, int maxv)
{
    float minvf = minv;
    float maxvf = maxv;
    bool ret;
    float vf = *v;
    ret = gui_input_float(label, &vf, 1, minvf, maxvf, "%.0f");
    if (ret) *v = vf;
    return ret;
}

static void label_aligned(const char *label, float size)
{
    ImVec2 spacing;
    float text_size = ImGui::CalcTextSize(label).x;
    const ImGuiStyle &style = ImGui::GetStyle();

    spacing = style.ItemSpacing;
    spacing.x = ITEM_SPACING.x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);

    ImGui::SetCursorPosX(size - text_size - ITEM_SPACING.x);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::PopStyleVar(1);
}

/*
 * Custom slider widget.
 */
static bool slider_float(float *v, float minv, float maxv, const char *format)
{
    bool ret;
    float step = (maxv - minv) * 0.008;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 rmin, rmax;
    float k;
    bool highlighted;
    ImVec4 color;
    ImU32 col;

    // Render an imgui DragFloat with transparent background.
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ret = ImGui::DragFloat("", v, step, minv, maxv, format);
    ImGui::PopStyleColor(3);
    highlighted = ImGui::IsItemHovered();

    // Render our own slider below the input.
    rmin = ImGui::GetItemRectMin();
    rmax = ImGui::GetItemRectMax();

    draw_list->ChannelsSetCurrent(0);
    k = (*v - minv) / (maxv - minv);

    color = COLOR(NUMBER_INPUT, INNER, false);
    if (highlighted) color = color_lighten(color);
    col = ImGui::GetColorU32(color);
    draw_list->AddRectFilled(rmin, rmax, col, 2);

    rmax.x = mix(rmin.x, rmax.x, k);
    color = COLOR(NUMBER_INPUT, ITEM, false);
    if (highlighted) color = color_lighten(color);
    col = ImGui::GetColorU32(color);
    draw_list->AddRectFilled(rmin, rmax, col, 2);

    draw_list->ChannelsMerge();
    return ret;
}

// To be called after imgui inputs function, to udpate our internal state.
static void update_activation_state(void)
{
    int i;

    gui->item_activated = false;
    gui->item_deactivated = false;

    if (ImGui::IsItemActivated()) {
        gui->item_activated = true;
        for (i = 0; i < gui->groups_count; i++) {
            gui->groups[i].activated = true;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        gui->item_deactivated = true;
        for (i = 0; i < gui->groups_count; i++) {
            gui->groups[i].deactivated = true;
        }
    }
}

bool gui_input_float(const char *label, float *v, float step,
                     float minv, float maxv, const char *format)
{
    bool ret = false;
    const char *left_utf = "◀";
    const char *right_utf = "▶";
    float v_speed = step / 10;
    bool unbounded;
    bool show_arrows = false;
    bool is_active = false;
    ImGuiID key = 0;
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 button_size = ImVec2(
            gui_get_item_height(),
            ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
    int v_int;
    char v_label[128];

    gui->item_activated = false;
    gui->item_deactivated = false;

    // To remove: use 0, 0 instead.
    if (minv == -FLT_MAX && maxv == +FLT_MAX) {
        assert(false);
        minv = 0;
        maxv = 0;
    }

    if (step == 0.f) step = 0.1f;
    if (!format) format = "%.1f";

    // Because imgui doesn't round to the step, we use DragInt with a
    // custom label to make it work properly.
    v_int = round(*v / step);
    snprintf(v_label, sizeof(v_label), format, v_int * step);
    unbounded = (minv == 0 && maxv == 0);

    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR(NUMBER_INPUT, INNER, false));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                    color_lighten(COLOR(NUMBER_INPUT, INNER, false)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                    color_lighten2(COLOR(NUMBER_INPUT, INNER, false)));
    ImGui::PushStyleColor(ImGuiCol_Button, COLOR(NUMBER_INPUT, INNER, false));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    color_lighten(COLOR(NUMBER_INPUT, INNER, false)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    color_lighten2(COLOR(NUMBER_INPUT, INNER, false)));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                    COLOR(NUMBER_INPUT, ITEM, false));

    label_aligned(label, LABEL_SIZE);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    ImGui::BeginGroup();
    ImGui::PushButtonRepeat(true);

    if (unbounded) {
        key = ImGui::GetID("show_arrows");
        show_arrows = storage->GetBool(key, false);
    }

    if (show_arrows) {
        if (ImGui::Button(left_utf, button_size)) {
            (*v) -= step;
            ret = true;
        }
        update_activation_state();

        ImGui::SameLine();
        ImGui::PushItemWidth(
                ImGui::GetContentRegionAvail().x - button_size.x - 4);
        if (ImGui::DragInt("", &v_int, v_speed, minv, maxv, v_label)) {
            *v  = v_int * step;
            ret = true;
        }
        update_activation_state();
        is_active = ImGui::IsItemActive();
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(right_utf, button_size)) {
            (*v) += step;
            ret = true;
        }
        update_activation_state();
    } else {
        ImGui::SetNextItemWidth(-1);
        if (unbounded) {
            if (ImGui::DragInt("", &v_int, step, minv, maxv, v_label)) {
                *v  = v_int * step;
                ret = true;
            }
        } else {
            ret = slider_float(v, minv, maxv, format);
        }
        update_activation_state();
        is_active = ImGui::IsItemActive();
    }

    ImGui::PopButtonRepeat();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(7);
    ImGui::EndGroup();
    if (unbounded) {
        storage->SetBool(key, ret || is_active || ImGui::IsItemHovered());
    }
    ImGui::PopID();

    if (ret) {
        if (minv != 0 || maxv != 0) {
            *v = clamp(*v, minv, maxv);
        }
        on_click();
    }
    return ret;
}

bool gui_bbox(float box[4][4])
{
    int x, y, z, w, h, d;
    bool ret = false;
    float p[3];
    w = box[0][0] * 2;
    h = box[1][1] * 2;
    d = box[2][2] * 2;
    x = round(box[3][0] - box[0][0]);
    y = round(box[3][1] - box[1][1]);
    z = round(box[3][2] - box[2][2]);

    gui_group_begin(_("Origin"));
    ret |= gui_input_int("x", &x, 0, 0);
    ret |= gui_input_int("y", &y, 0, 0);
    ret |= gui_input_int("z", &z, 0, 0);
    gui_group_end();
    gui_group_begin(_("Size"));
    ret |= gui_input_int("w", &w, 0, 0);
    ret |= gui_input_int("h", &h, 0, 0);
    ret |= gui_input_int("d", &d, 0, 0);
    w = max(1, w);
    h = max(1, h);
    d = max(1, d);
    gui_group_end();

    if (ret) {
        vec3_set(p, x + w / 2., y + h / 2., z + d / 2.);
        bbox_from_extents(box, p, w / 2., h / 2., d / 2.);
    }
    return ret;
}

bool gui_angle(const char *id, float *v, int vmin, int vmax)
{
    int a;
    bool ret;
    a = round(*v * DR2D);
    ret = gui_input_int(id, &a, vmin, vmax);
    if (ret) {
        if (vmin == 0 && vmax == 360) {
            while (a < 0) a += 360;
            a %= 360;
        }
        if (vmin != 0 || vmax != 0)
            a = clamp(a, vmin, vmax);
        *v = (float)(a * DD2R);
    }
    return ret;
}

bool gui_action_button(int id, const char *label, float size)
{
    bool ret;
    const action_t *action;

    action = action_get(id, true);
    assert(action);
    ImGui::PushID(action->id);
    ret = gui_button(label, size, action->icon);
    if (ImGui::IsItemHovered() && action->help)
        goxel_add_hint(0, NULL, tr(action->help));
    if (ret) {
        action_exec(action_get(id, true));
    }
    ImGui::PopID();
    if (gui->is_row) ImGui::SameLine();
    return ret;
}

static bool _selectable(const char *label, bool *v, const char *tooltip,
                        float w, int icon)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 size;
    ImVec2 center;
    ImVec2 p1, p2;
    bool ret = false;
    bool default_v = false;
    ImVec2 uv0, uv1; // The position in the icon texture.
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle &style = ImGui::GetStyle();

    if (gui->item_size) w = gui->item_size;

    v = v ? v : &default_v;
    size = (icon != -1) ?
        ImVec2(GUI_ICON_HEIGHT, GUI_ICON_HEIGHT) :
        ImVec2(w, gui_get_item_height());

    if (!tooltip && icon != -1) {
        tooltip = label;
        while (*tooltip == '#') tooltip++;
    }

    ImGui::PushID(label);
    if (icon == -1) {
        ImGui::PushStyleColor(ImGuiCol_Button, COLOR(SELECTABLE, INNER, (*v)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                color_lighten(COLOR(SELECTABLE, INNER, true)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                color_lighten2(COLOR(SELECTABLE, INNER, true)));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, COLOR(ICON, INNER, (*v)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                color_lighten(COLOR(ICON, INNER, true)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                color_lighten2(COLOR(ICON, INNER, true)));
    }

    if (icon != -1) {
        ret = ImGui::Button("", size);
        p1 = ImGui::GetItemRectMin();
        p2 = ImGui::GetItemRectMax();
        if (icon) {
            center = (p1 + p2) / 2;
            center.y += 0.5;
            uv0 = get_icon_uv(icon);
            uv1 = uv0 + ImVec2(1. / 8, 1. / 8);
            window->DrawList->AddImage(
                    (intptr_t)g_tex_icons->tex,
                    center - ImVec2(16, 16),
                    center + ImVec2(16, 16),
                    uv0, uv1, get_icon_color(icon, *v));
        }
        if (*v) {
            const ImU32 active = IM_COL32(64, 220, 255, 255);
            const ImU32 inner = IM_COL32(255, 255, 255, 220);
            draw_list->AddRect(p1 - ImVec2(1, 1), p2 + ImVec2(1, 1),
                               active, style.FrameRounding, 0, 3.0f);
            draw_list->AddRect(p1 + ImVec2(2, 2), p2 - ImVec2(2, 2),
                               inner, style.FrameRounding, 0, 1.0f);
            draw_list->AddRectFilled(
                    ImVec2(p1.x + 5, p2.y - 5),
                    ImVec2(p2.x - 5, p2.y - 2),
                    active, 1.0f);
        }
    } else {
        ret = ImGui::Button(label, size);
    }
    ImGui::PopStyleColor(3);
    if (ret) *v = !*v;
    if (tooltip && ImGui::IsItemHovered()) {
        gui_tooltip(tooltip);
        goxel_add_hint(0, NULL, tooltip);
    }
    ImGui::PopID();
    if (gui->is_row) ImGui::SameLine();

    if (ret) on_click();
    return ret;
}

bool gui_selectable(const char *name, bool *v, const char *tooltip, float w)
{
    return _selectable(name, v, tooltip, w, -1);
}

bool gui_selectable_toggle(const char *name, int *v, int set_v,
                           const char *tooltip, float w)
{
    bool b = *v == set_v;
    if (gui_selectable(name, &b, tooltip, w)) {
        if (b) *v = set_v;
        return true;
    }
    return false;
}

bool gui_selectable_icon(const char *name, bool *v, int icon)
{
    return _selectable(name, v, NULL, 0, icon);
}

void gui_text(const char *label, ...)
{
    va_list args;
    va_start(args, label);
    ImGui::TextV(label, args);
    va_end(args);
}

void gui_text_wrapped(const char *label, ...)
{
    va_list args;
    ImGui::PushTextWrapPos(0);
    va_start(args, label);
    ImGui::TextV(label, args);
    va_end(args);
    ImGui::PopTextWrapPos();
}

void gui_dummy(int w, int h)
{
    ImGui::Dummy(ImVec2(w, h));
}

void gui_spacing(int w)
{
    ImGui::Dummy(ImVec2(w, 0));
    ImGui::SameLine();
}

static bool color_picker(const char *label, uint8_t color[4])
{
    float colorf[4] = {color[0] / 255.f,
                       color[1] / 255.f,
                       color[2] / 255.f,
                       color[3] / 255.f};
    static uint8_t backup_color[4];
    bool ret;

    if (ImGui::IsWindowAppearing())
        memcpy(backup_color, color, sizeof(backup_color));
    ret = ImGui::ColorPicker4(label, colorf,
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoSmallPreview);
    if (ret) {
        color[0] = colorf[0] * 255;
        color[1] = colorf[1] * 255;
        color[2] = colorf[2] * 255;
        color[3] = colorf[3] * 255;
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Current");
    ImGui::ColorButton("##current", color,
            ImGuiColorEditFlags_NoPicker,
            ImVec2(60 * COLOR_SWATCH_SCALE, 40 * COLOR_SWATCH_SCALE));
    ImGui::Text("Original");
    if (ImGui::ColorButton("##previous", backup_color,
                ImGuiColorEditFlags_NoPicker,
                ImVec2(60 * COLOR_SWATCH_SCALE,
                       40 * COLOR_SWATCH_SCALE))) {
        memcpy(color, backup_color, sizeof(backup_color));
        ret = true;
    }

    ImGui::EndGroup();
    return ret;
}

bool gui_color(const char *label, uint8_t color[4])
{
    bool ret = false;
    ImVec2 size = color_swatch_size();

    ImGui::PushID(label);
    align_toolbar_item(size);
    if (ImGui::ColorButton(label, color, 0, size)) {
        ImGui::OpenPopup("GoxelPicker");
    }

    if (ImGui::BeginPopupContextItem("GoxelPicker")) {
        if (color_picker(label, color)) {
            ret = true;
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    if (gui->is_row) ImGui::SameLine();
    return ret;
}

bool gui_color_swatch(const char *label, uint8_t color[4],
                      uint8_t active_color[4])
{
    bool ret = false;
    ImVec2 size = color_swatch_size();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::PushID(label);
    align_toolbar_item(size);
    if (ImGui::ColorButton(label, color, 0, size)) {
        vec4_copy(color, active_color);
        on_click();
        ret = true;
    }

    if (ImGui::BeginPopupContextItem("GoxelPicker")) {
        if (color_picker(label, color)) {
            vec4_copy(color, active_color);
            ret = true;
        }
        ImGui::EndPopup();
    }
    if (active_color && memcmp(color, active_color, 4) == 0) {
        ImVec2 c1 = ImGui::GetItemRectMin() - ImVec2(1, 1);
        ImVec2 c2 = ImGui::GetItemRectMax() + ImVec2(1, 1);
        draw_list->AddRect(c1, c2, IM_COL32(64, 220, 255, 255), 0, 0, 2);
        draw_list->AddRect(c1, c2, IM_COL32(0, 0, 0, 255), 0, 0, 1);
    }

    ImGui::PopID();
    if (gui->is_row) ImGui::SameLine();
    return ret;
}

bool gui_color_small(const char *label, uint8_t color[4])
{
    bool ret = false;

    ImGui::PushID(label);
    label_aligned(label, LABEL_SIZE);
    if (ImGui::ColorButton("##color", color, 0, color_small_swatch_size())) {
        ImGui::OpenPopup("GoxelPicker");
    }

    if (ImGui::BeginPopupContextItem("GoxelPicker")) {
        if (color_picker(label, color)) {
            ret = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return ret;
}

bool gui_color_small_f3(const char *label, float color[3])
{
    uint8_t c[4];
    bool ret;
    rgb_to_srgb8(color, c);
    c[3] = 255;
    ret = gui_color_small(label, c);
    srgb8_to_rgb(c, color);
    return ret;
}

bool gui_checkbox(const char *label, bool *v, const char *hint)
{
    bool ret;
    label_aligned("", LABEL_SIZE);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR(CHECKBOX, INNER, false));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, COLOR(CHECKBOX, ITEM, false));
    ret = ImGui::Checkbox(label, v);
    if (hint && ImGui::IsItemHovered()) gui_tooltip(hint);
    if (ret) on_click();
    ImGui::PopStyleColor(2);
    return ret;
}

bool gui_checkbox_flag(const char *label, int *v, int flag, const char *hint)
{
    bool ret, b;
    b = (*v) & flag;
    ret = gui_checkbox(label, &b, hint);
    if (ret) {
        if (b) *v |= flag;
        else   *v &= ~flag;
    }
    return ret;
}


bool gui_button(const char *label, float size, int icon)
{
    bool ret;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 uv0, uv1;
    ImVec2 button_size;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 center;
    int w, isize;

    button_size = ImVec2(size * ImGui::GetContentRegionAvail().x,
                         gui_get_item_height());
    if (size == -1) button_size.x = ImGui::GetContentRegionAvail().x;
    if (size == 0 && (label == NULL || label[0] == '#')) {
        button_size.x = GUI_ICON_HEIGHT;
        button_size.y = GUI_ICON_HEIGHT;
    }
    if (size == 0 && label && label[0] != '#') {
        w = ImGui::CalcTextSize(label, NULL, true).x + style.FramePadding.x * 2;
        if (w < gui_get_item_height())
            button_size.x = gui_get_item_height();
    }

    if (gui->item_size) button_size.x = gui->item_size;

    isize = (label && label[0] != '#') ? 12 : 16;
    ImGui::PushStyleColor(ImGuiCol_Button,
            (label && (label[0] != '#')) ?
            COLOR(BUTTON, INNER, false) : COLOR(ICON, INNER, false));
    ret = ImGui::Button(label ?: "", button_size);
    update_activation_state();
    ImGui::PopStyleColor();
    if (icon) {
        center = ImGui::GetItemRectMin() +
            ImVec2(ImGui::GetItemRectSize().y / 2,
                   ImGui::GetItemRectSize().y / 2);
        uv0 = ImVec2(((icon - 1) % 8) / 8.0, ((icon - 1) / 8) / 8.0);
        uv1 = ImVec2(uv0.x + 1. / 8, uv0.y + 1. / 8);
        draw_list->AddImage((intptr_t)g_tex_icons->tex,
                            center - ImVec2(isize, isize),
                            center + ImVec2(isize, isize),
                            uv0, uv1, get_icon_color(icon, 0));
    }
    if (ret) {
        on_click();
        if (gui->is_context_menu)  {
            ImGui::CloseCurrentPopup();
        }
    }
    if (gui->is_row) ImGui::SameLine();
    return ret;
}

bool gui_button_right(const char *label, int icon)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    float text_size = ImGui::CalcTextSize(label).x;
    float w = text_size + 2 * style.FramePadding.x;
    w = max(w, gui_get_item_height());
    w += style.FramePadding.x;
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - w, 0));
    ImGui::SameLine();
    return gui_button(label, 0, icon);
}

bool gui_input_text(const char *label, char *txt, int size)
{
    bool ret;
    ret = ImGui::InputText(label, txt, size);
    update_activation_state();
    return ret;

}

bool gui_input_text_multiline(const char *label, char *buf, int size,
                              float width, float height)
{
    // We set the frame color to a semi transparent value, because otherwise
    // we cannot render the error highlight.
    // XXX: fix that.
    bool ret;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 col = style.Colors[ImGuiCol_FrameBg];
    style.Colors[ImGuiCol_FrameBg].w = 0.5;
    ret = ImGui::InputTextMultiline(label, buf, size, ImVec2(width, height));
    style.Colors[ImGuiCol_FrameBg] = col;
    return ret;
}

bool gui_combo(const char *label, int *v, const char **names, int nb)
{
    bool ret;
    ImGui::PushItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR(COMBO, INNER, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(COMBO, BACKGROUND, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, COLOR(COMBO, ITEM, 0));
    ret = ImGui::Combo(label, v, names, nb);
    ImGui::PopStyleColor(3);
    ImGui::PopItemWidth();
    return ret;
}

bool gui_combo_begin(const char *label, const char *preview)
{
    bool ret;

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR(COMBO, INNER, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(COMBO, BACKGROUND, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, COLOR(COMBO, ITEM, 0));
    ret = ImGui::BeginCombo(label, preview);

    if (!ret) {
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
    }
    return ret;
}

void gui_combo_end(void)
{
    ImGui::EndCombo();
    ImGui::PopStyleColor(3);
    ImGui::PopItemWidth();
}

bool gui_combo_item(const char *label, bool is_selected)
{
    bool ret;
    ret = ImGui::Selectable(label, is_selected);
    if (is_selected)
        ImGui::SetItemDefaultFocus();
    return ret;
}

void gui_input_text_multiline_highlight(int line)
{
    float h = ImGui::CalcTextSize("").y;
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    rmin.y = rmin.y + line * h + 2;
    rmax.y = rmin.y + h;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rmin, rmax, 0xff0000ff);
}

void gui_enabled_begin(bool enabled)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 color = style.Colors[ImGuiCol_Text];
    if (!enabled) color.w /= 2;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::BeginDisabled(!enabled);
}

void gui_enabled_end(void)
{
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
}

// To remove?
static bool gui_quat(float q[4])
{
    // Hack to prevent weird behavior when we change the euler angles.
    // We keep track of the last used euler angles value and reuse them if
    // the quaternion is the same.
    static struct {
        float quat[4];
        float eul[3];
    } last = {};
    float eul[3];
    bool ret = false;

    if (memcmp(q, &last.quat, sizeof(last.quat)) == 0)
        vec3_copy(last.eul, eul);
    else
        quat_to_eul(q, EULER_ORDER_DEFAULT, eul);
    if (gui_angle("x", &eul[0], 0, 0)) ret = true;
    if (gui_angle("y", &eul[1], 0, 0)) ret = true;
    if (gui_angle("z", &eul[2], 0, 0)) ret = true;

    if (ret) {
        eul_to_quat(eul, EULER_ORDER_DEFAULT, q);
        quat_copy(q, last.quat);
        vec3_copy(eul, last.eul);
    }
    return ret;
}

bool gui_rotation_mat4(float m[4][4])
{
    float rot[3][3], quat[4];
    bool ret = false;
    int i, j;

    mat4_to_mat3(m, rot);
    mat3_to_quat(rot, quat);
    if (gui_quat(quat)) {
        quat_to_mat3(quat, rot);
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                m[i][j] = rot[i][j];
        ret = true;
    }
    return ret;
}

bool gui_rotation_mat4_axis(float m[4][4])
{
    float rot[3][3];
    bool ret = false;
    bool v;
    int i, j;
    const struct {
        const char *label;
        float rot[3][3];
    } axis[] = {
        {"+X", {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}},
        {"-X", {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}},
        {"+Y", {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}},
        {"-Y", {{1, 0, 0}, {0, 0, 1}, {-1, 0, 0}}},
        {"+Z", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}},
        {"-Z", {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}}},
    };

    mat4_to_mat3(m, rot);

    for (i = 0; i < 3; i++) {
        gui_row_begin(2);
        for (j = 0; j < 2; j++) {
            v = memcmp(rot, axis[i * 2 + j].rot, sizeof(rot)) == 0;
            if (gui_selectable(axis[i * 2 + j].label, &v, NULL, -1)) {
                memcpy(rot, axis[i * 2 + j].rot, sizeof(rot));
                ret = true;
            }
        }
        gui_row_end();
    }
    if (ret) {
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                m[i][j] = rot[i][j];
    }
    return ret;
}

void gui_open_popup(const char *title, int flags, void *data,
                    int (*func)(void *data))
{
    typeof(gui->popup[0]) *popup;
    popup = &gui->popup[gui->popup_count++];
    popup->title = title;
    popup->func = func;
    popup->flags = flags;
    assert(!popup->data);
    popup->data = data;
}

void gui_on_popup_closed(void (*func)(int))
{
    gui->popup[gui->popup_count - 1].on_closed = func;
}

void gui_popup_bottom_begin(void)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    float bottom_y = gui_get_item_height() + style.FramePadding.y * 2;
    float w = ImGui::GetContentRegionAvail().y - bottom_y;
    ImGui::Dummy(ImVec2(0, w));
    gui_row_begin(0);
}

void gui_popup_bottom_end(void)
{
    gui_row_end();
}

void gui_alert(const char *title, const char *msg)
{
    gui_open_popup(title, 0, msg ? strdup(msg) : NULL, alert_popup);
}

bool gui_collapsing_header(const char *label, bool default_opened)
{
    if (default_opened)
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    return ImGui::CollapsingHeader(label);
}

void gui_columns(int count)
{
    ImGui::Columns(count);
}

void gui_next_column(void)
{
    ImGui::NextColumn();
}

void gui_separator(void)
{
    ImGui::Separator();
}

void gui_push_id(const char *id)
{
    ImGui::PushID(id);
}

void gui_pop_id(void)
{
    ImGui::PopID();
}

void gui_request_panel_width(float width)
{
    goxel.gui.panel_width = width;
}

bool gui_layer_item(int idx, int icons_count, const int *icons,
                    bool *visible, bool *selected,
                    char *name, int len)
{
    bool ret = false;
    bool selected_ = *selected;
    static char *edit_name = NULL;
    static bool start_edit;
    float font_size = ImGui::GetFontSize();
    int icon;
    int i;
    ImVec2 center;
    ImVec2 uv0, uv1;
    ImVec2 padding;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::PushID(idx);
    ImGui::PushStyleColor(ImGuiCol_Button, COLOR(WIDGET, INNER, *selected));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            color_lighten(COLOR(WIDGET, INNER, *selected)));
    if (visible) {
        if (gui_selectable_icon("##visible", &selected_,
                *visible ? ICON_VISIBILITY : ICON_VISIBILITY_OFF)) {
            *visible = !*visible;
            ret = true;
        }
        ImGui::SameLine();
    }

    if (edit_name != name) {
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5));
        padding = style.FramePadding;
        padding.x += GUI_ICON_HEIGHT * 0.75 * icons_count;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
        if (ImGui::Button(name, ImVec2(-1, GUI_ICON_HEIGHT))) {
            *selected = true;
            ret = true;
        }
        ImGui::PopStyleVar();

        for (i = 0; i < icons_count; i++) {
            icon = icons[i];
            center = ImGui::GetItemRectMin() +
                ImVec2(GUI_ICON_HEIGHT * 0.75 * (i + 0.5), GUI_ICON_HEIGHT / 2);
            uv0 = ImVec2(((icon - 1) % 8) / 8.0, ((icon - 1) / 8) / 8.0);
            uv1 = ImVec2(uv0.x + 1. / 8, uv0.y + 1. / 8);
            draw_list->AddImage(
                    (intptr_t)g_tex_icons->tex,
                    center - ImVec2(12, 12),
                    center + ImVec2(12, 12),
                    uv0, uv1, get_icon_color(icon, 0));
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            edit_name = name;
            start_edit = true;
        }
    } else {
        if (start_edit) ImGui::SetKeyboardFocusHere();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x,
                            (GUI_ICON_HEIGHT - font_size) / 2));
        ImGui::InputText("##name_edit", name, len,
                         ImGuiInputTextFlags_AutoSelectAll);
        if (!start_edit && !ImGui::IsItemActive()) edit_name = NULL;
        start_edit = false;
        ImGui::PopStyleVar();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return ret;
}

bool gui_is_key_down(int key)
{
    return ImGui::IsKeyDown((ImGuiKey)key);
}

bool gui_menu_bar_begin(void)
{
    bool ret;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, COLOR(MENU, BACKGROUND, false));
    ImGui::PushStyleColor(ImGuiCol_Header,
            color_lighten(COLOR(MENU, BACKGROUND, false)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            color_lighten(COLOR(MENU, BACKGROUND, false)));
    ImGui::PushStyleColor(ImGuiCol_Text, COLOR(MENU, TEXT, false));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(MENU, BACKGROUND, false));

    ret = ImGui::BeginMainMenuBar();
    if (!ret) {
        ImGui::PopStyleColor(5);
    }
    return ret;
}

void gui_menu_bar_end(void)
{
    ImGui::PopStyleColor(5);
    ImGui::EndMainMenuBar();
}


bool gui_menu_begin(const char *label, bool enabled)
{
    return ImGui::BeginMenu(label, enabled);
}

void gui_menu_end(void)
{
    ImGui::EndMenu();
}

bool gui_menu_item(int action, const char *label, bool enabled)
{
    const action_t *a = NULL;
    if (action) {
        a = action_get(action, true);
        assert(a);
    }
    if (ImGui::MenuItem(label, a ? a->shortcut : NULL, false, enabled)) {
        if (a) action_exec(a);
        return true;
    }
    return false;
}

void gui_tooltip(const char *str)
{
    if (gui->scrolling) return;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(TOOLTIP, BACKGROUND, 0));
    ImGui::SetTooltip("%s", str);
    ImGui::PopStyleColor();
}

bool gui_tab(const char *label, int icon, bool *v)
{
    return _selectable(label, v, NULL, 0, icon);
}

bool gui_toolbar_handle(const char *tooltip)
{
    bool ret = false;
    bool active;
    bool hovered;
    float size = GUI_ICON_HEIGHT;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 p1, p2, delta;
    ImU32 bg_col;
    ImU32 line_col;

    ImGui::PushID(tooltip ? tooltip : "toolbar_handle");
    ImGui::InvisibleButton("##toolbar_handle", ImVec2(size, size));
    update_activation_state();

    hovered = ImGui::IsItemHovered();
    active = ImGui::IsItemActive();
    if (active && ImGui::IsMouseDragging(0)) {
        delta = ImGui::GetIO().MouseDelta;
        if (delta.x || delta.y) {
            ImGui::SetWindowPos(ImGui::GetWindowPos() + delta);
            ret = true;
        }
    }
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (tooltip) gui_tooltip(tooltip);
    }

    p1 = ImGui::GetItemRectMin();
    p2 = ImGui::GetItemRectMax();
    bg_col = active ? IM_COL32(58, 58, 58, 255) :
             hovered ? IM_COL32(48, 48, 48, 255) :
                       IM_COL32(38, 38, 38, 255);
    line_col = IM_COL32(150, 150, 150, 255);
    draw_list->AddRectFilled(p1, p2, bg_col, style.FrameRounding);
    for (int i = 0; i < 3; i++) {
        float x = p1.x + size * 0.38f + i * 4.0f;
        draw_list->AddLine(ImVec2(x, p1.y + size * 0.28f),
                           ImVec2(x, p2.y - size * 0.28f),
                           line_col, 1.5f);
    }

    ImGui::PopID();
    if (gui->is_row) ImGui::SameLine();
    return ret;
}

static bool gui_toolbar_fold_button(bool *folded)
{
    bool ret = false;
    bool active;
    bool hovered;
    float size = GUI_ICON_HEIGHT;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 p1, p2, center;
    ImU32 bg_col, arrow_col;
    ImVec2 points[3];

    if (!folded) return false;
    ImGui::PushID("toolbar_fold");
    if (ImGui::InvisibleButton("##toolbar_fold", ImVec2(size, size))) {
        *folded = !*folded;
        settings_save();
        ret = true;
    }
    update_activation_state();

    hovered = ImGui::IsItemHovered();
    active = ImGui::IsItemActive();
    if (hovered) {
        gui_tooltip(*folded ? "Unfold toolbar" : "Fold toolbar");
    }

    p1 = ImGui::GetItemRectMin();
    p2 = ImGui::GetItemRectMax();
    center = (p1 + p2) * 0.5f;
    bg_col = active ? IM_COL32(58, 58, 58, 255) :
             hovered ? IM_COL32(48, 48, 48, 255) :
                       IM_COL32(38, 38, 38, 255);
    arrow_col = IM_COL32(178, 178, 178, 255);
    draw_list->AddRectFilled(p1, p2, bg_col, style.FrameRounding);
    if (*folded) {
        points[0] = ImVec2(center.x - 3.0f, center.y - 7.0f);
        points[1] = ImVec2(center.x - 3.0f, center.y + 7.0f);
        points[2] = ImVec2(center.x + 5.0f, center.y);
    } else {
        points[0] = ImVec2(center.x + 4.0f, center.y - 7.0f);
        points[1] = ImVec2(center.x + 4.0f, center.y + 7.0f);
        points[2] = ImVec2(center.x - 5.0f, center.y);
    }
    draw_list->AddTriangleFilled(points[0], points[1], points[2], arrow_col);
    ImGui::PopID();
    return ret;
}

bool gui_toolbar_chrome(const char *tooltip, bool *folded)
{
    bool ret;

    ret = gui_toolbar_handle(tooltip);
    if (!gui->is_row) ImGui::SameLine();
    ret |= gui_toolbar_fold_button(folded);
    if (gui->is_row && folded && !*folded) ImGui::SameLine();
    return ret;
}

static void draw_resize_corners(const ImVec2& pos, const ImVec2& size,
                                bool visible)
{
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 col = visible ? IM_COL32(64, 220, 255, 220) :
                          IM_COL32(64, 220, 255, 0);
    float len = 18.0f;
    float inset = 4.0f;
    float thick = 2.0f;
    ImVec2 bl = ImVec2(pos.x + inset, pos.y + size.y - inset);
    ImVec2 br = ImVec2(pos.x + size.x - inset, pos.y + size.y - inset);

    draw_list->AddLine(bl, ImVec2(bl.x + len, bl.y), col, thick);
    draw_list->AddLine(bl, ImVec2(bl.x, bl.y - len), col, thick);
    draw_list->AddLine(br, ImVec2(br.x - len, br.y), col, thick);
    draw_list->AddLine(br, ImVec2(br.x, br.y - len), col, thick);
}

static void push_utility_window_style(bool chrome_visible, float alpha)
{
    float bg_alpha = chrome_visible ? alpha : 0.0f;
    float title_alpha = chrome_visible ? 1.0f : 0.0f;

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.16f, 0.16f, 0.16f, bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_Text,
            color_with_alpha(COLOR(BASE, TEXT, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border,
            ImVec4(0.35f, 0.35f, 0.35f, title_alpha));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 0.45f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 0.85f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 1.0f : 0.0f));
}

static bool utility_window_hovered(void)
{
    return ImGui::IsWindowHovered(
            ImGuiHoveredFlags_RootAndChildWindows |
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

static bool reference_image_titlebar_alpha_slider(const char *label,
                                                  float *alpha,
                                                  bool chrome_visible,
                                                  bool *active,
                                                  bool *store_now)
{
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImRect title_bar = window->TitleBarRect();
    ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
    float close_space;
    float label_right;
    float slider_right;
    float slider_w;
    float slider_h;
    float percent;
    bool ret = false;

    if (active)
        *active = false;
    if (!chrome_visible || !alpha)
        return false;

    close_space = style.FramePadding.x;
    if (window->HasCloseButton)
        close_space += title_bar.GetHeight() + style.ItemInnerSpacing.x;
    slider_right = title_bar.Max.x - close_space;
    label_right = title_bar.Min.x + style.FramePadding.x +
        ImGui::CalcTextSize(label, NULL, true).x + style.ItemInnerSpacing.x;
    slider_w = min(110.0f, slider_right - label_right);
    if (slider_w < 54.0f)
        return false;

    slider_h = max(10.0f, title_bar.GetHeight() - style.FramePadding.y * 2.0f);
    percent = clamp(*alpha, 0.05f, 1.0f) * 100.0f;

    ImGui::PushClipRect(title_bar.Min, title_bar.Max, false);
    ImGui::SetCursorScreenPos(
            ImVec2(slider_right - slider_w,
                   title_bar.Min.y + (title_bar.GetHeight() - slider_h) * 0.5f));
    ImGui::PushItemWidth(slider_w);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(style.FramePadding.x,
                   max(0.0f, (slider_h - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
            ImVec4(0.10f, 0.10f, 0.10f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4(0.14f, 0.14f, 0.14f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
            ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,
            ImVec4(0.25f, 0.86f, 1.0f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
            ImVec4(0.25f, 0.86f, 1.0f, 1.0f));
    if (ImGui::SliderFloat("##reference_title_alpha", &percent,
                           5.0f, 100.0f, "%.0f%%",
                           ImGuiSliderFlags_NoInput)) {
        *alpha = clamp(percent / 100.0f, 0.05f, 1.0f);
        ret = true;
    }
    if (active)
        *active = ImGui::IsItemActive();
    if (store_now && ImGui::IsItemDeactivated())
        *store_now = true;
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(saved_cursor);
    return ret;
}

bool gui_reference_image_window(const char *label, texture_t *texture,
                                float pos[2], float size[2],
                                bool *pos_set, bool *size_set,
                                float pan[2], float *zoom, float *alpha,
                                bool *visible)
{
    static bool chrome_visible = false;
    static bool pending_settings_save = false;
    bool ret = false;
    bool dirty = false;
    bool store_now = false;
    bool opened;
    bool hovered;
    bool titlebar_alpha_active = false;
    float bg_alpha;
    float chrome_alpha;
    float title_alpha;
    float fit;
    float old_zoom;
    ImVec2 default_size;
    ImVec2 win_pos;
    ImVec2 win_size;
    ImVec2 canvas_pos;
    ImVec2 canvas_size;
    ImVec2 image_size;
    ImVec2 image_pos;
    ImVec2 mouse;
    ImVec2 uv1;
    ImDrawList *draw_list;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (!visible || !*visible || !texture)
        return false;
    if (!alpha || *alpha <= 0.0f)
        *alpha = 0.40f;
    *alpha = clamp(*alpha, 0.05f, 1.0f);
    bg_alpha = *alpha;
    chrome_alpha = chrome_visible ? bg_alpha : 0.0f;
    title_alpha = chrome_visible ? 1.0f : 0.0f;

    default_size = ImVec2(320.0f, 240.0f);
    if (!*size_set) {
        size[0] = default_size.x;
        size[1] = default_size.y;
    }
    if (!*pos_set) {
        pos[0] = max(0.0f, io.DisplaySize.x - size[0] - 16.0f);
        pos[1] = 180.0f;
    }
    pos[0] = clamp(pos[0], 0.0f, max(0.0f, io.DisplaySize.x - 80.0f));
    pos[1] = clamp(pos[1], 0.0f, max(0.0f, io.DisplaySize.y - 80.0f));
    size[0] = max(size[0], 180.0f);
    size[1] = max(size[1], 140.0f);
    *zoom = clamp(*zoom <= 0.0f ? 1.0f : *zoom, 0.1f, 32.0f);

    ImGui::SetNextWindowPos(ImVec2(pos[0], pos[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(size[0], size[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 140.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));

    opened = *visible;
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0.22f, 0.22f, 0.22f, chrome_alpha));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(0.22f, 0.22f, 0.22f, chrome_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,
            color_with_alpha(COLOR(WINDOW, BACKGROUND, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_Text,
            color_with_alpha(COLOR(BASE, TEXT, false), title_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border,
            ImVec4(0.35f, 0.35f, 0.35f, title_alpha));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 0.45f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 0.85f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,
            ImVec4(0.25f, 0.86f, 1.0f, chrome_visible ? 1.0f : 0.0f));
    if (!ImGui::Begin(label, &opened, flags)) {
        ImGui::End();
        ImGui::PopStyleColor(10);
        if (opened != *visible) {
            *visible = opened;
            return true;
        }
        return false;
    }

    if (reference_image_titlebar_alpha_slider(label, alpha, chrome_visible,
                                              &titlebar_alpha_active,
                                              &store_now))
        dirty = true;

    canvas_pos = ImGui::GetCursorScreenPos();
    canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = max(canvas_size.x, 1.0f);
    canvas_size.y = max(canvas_size.y, 1.0f);
    draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_pos, canvas_pos + canvas_size,
                             ImGui::GetColorU32(ImGuiCol_FrameBg));

    fit = min(canvas_size.x / texture->w, canvas_size.y / texture->h);
    fit = max(fit, 0.01f);
    image_size = ImVec2(texture->w * fit * *zoom,
                        texture->h * fit * *zoom);
    image_pos = canvas_pos + (canvas_size - image_size) * 0.5f +
                ImVec2(pan[0], pan[1]);

    ImGui::InvisibleButton("##reference_image_canvas", canvas_size,
                           ImGuiButtonFlags_MouseButtonMiddle);
    update_activation_state();
    hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (io.MouseWheel != 0.0f) {
            mouse = io.MousePos;
            old_zoom = *zoom;
            *zoom = clamp(*zoom * max(0.2f, 1.0f + io.MouseWheel * 0.12f),
                          0.1f, 32.0f);
            if (*zoom != old_zoom) {
                ImVec2 old_image_pos = image_pos;
                ImVec2 image_coord =
                    (mouse - old_image_pos) / max(fit * old_zoom, 0.01f);
                image_size = ImVec2(texture->w * fit * *zoom,
                                    texture->h * fit * *zoom);
                image_pos = mouse - image_coord * (fit * *zoom);
                pan[0] = image_pos.x -
                    (canvas_pos.x + (canvas_size.x - image_size.x) * 0.5f);
                pan[1] = image_pos.y -
                    (canvas_pos.y + (canvas_size.y - image_size.y) * 0.5f);
                dirty = true;
                store_now = true;
            }
        }
        if (ImGui::IsMouseDoubleClicked(2)) {
            *zoom = 1.0f;
            pan[0] = 0.0f;
            pan[1] = 0.0f;
            image_size = ImVec2(texture->w * fit * *zoom,
                                texture->h * fit * *zoom);
            image_pos = canvas_pos + (canvas_size - image_size) * 0.5f;
            dirty = true;
            store_now = true;
        }
    }
    if (ImGui::IsMouseDragging(2) && ImGui::IsItemActive()) {
        pan[0] += io.MouseDelta.x;
        pan[1] += io.MouseDelta.y;
        image_pos = image_pos + io.MouseDelta;
        dirty = true;
    }

    draw_list->PushClipRect(canvas_pos, canvas_pos + canvas_size, true);
    uv1 = ImVec2((float)texture->w / texture->tex_w,
                 (float)texture->h / texture->tex_h);
    draw_list->AddImage((intptr_t)texture->tex, image_pos,
                        image_pos + image_size, ImVec2(0, 0), uv1);
    draw_list->AddRect(canvas_pos, canvas_pos + canvas_size,
                       ImGui::GetColorU32(ImGuiCol_Border));
    draw_list->PopClipRect();

    win_pos = ImGui::GetWindowPos();
    win_size = ImGui::GetWindowSize();
    chrome_visible = utility_window_hovered() || titlebar_alpha_active;
    draw_resize_corners(win_pos, win_size, chrome_visible);
    if (!*pos_set || win_pos.x != pos[0] || win_pos.y != pos[1]) {
        pos[0] = win_pos.x;
        pos[1] = win_pos.y;
        *pos_set = true;
        dirty = true;
    }
    if (!*size_set || win_size.x != size[0] || win_size.y != size[1]) {
        size[0] = win_size.x;
        size[1] = win_size.y;
        *size_set = true;
        dirty = true;
    }
    if (opened != *visible) {
        *visible = opened;
        dirty = true;
        store_now = true;
    }

    ImGui::End();
    ImGui::PopStyleColor(10);

    if (dirty)
        pending_settings_save = true;
    if (pending_settings_save && (store_now || !ImGui::IsAnyMouseDown())) {
        pending_settings_save = false;
        ret = true;
    }
    return ret;
}

bool gui_view_cube_window(const char *label, float pos[2], float size[2],
                          bool *pos_set, bool *size_set)
{
    static bool chrome_visible = false;
    bool dirty = false;
    bool camera_changed;
    ImGuiIO& io = ImGui::GetIO();
    camera_t *camera = goxel.image->active_camera;
    float view[4][4];
    float view_prev[4][4];
    const float *projection = (float*)camera->proj_mat;
    const float zup2yup[4][4] = {
        {1, 0, 0, 0},
        {0, 0, -1, 0},
        {0, 1, 0, 0},
        {0, 0, 0, 1},
    };
    const float yup2zup[4][4] = {
        {1, 0, 0, 0},
        {0, 0, 1, 0},
        {0, -1, 0, 0},
        {0, 0, 0, 1},
    };
    ImVec2 canvas_pos;
    ImVec2 canvas_size;
    ImVec2 win_pos;
    ImVec2 win_size;
    ImGuizmo::Style &style = ImGuizmo::GetStyle();

    if (!*size_set) {
        size[0] = 128.0f;
        size[1] = 128.0f;
    }
    if (!*pos_set) {
        pos[0] = max(0.0f, io.DisplaySize.x - size[0] - 16.0f);
        pos[1] = 48.0f;
    }
    pos[0] = clamp(pos[0], 0.0f, max(0.0f, io.DisplaySize.x - 64.0f));
    pos[1] = clamp(pos[1], 0.0f, max(0.0f, io.DisplaySize.y - 64.0f));
    size[0] = max(size[0], 96.0f);
    size[1] = max(size[1], 96.0f);

    style.Colors[ImGuizmo::DIRECTION_X] =
        ImVec4(0.666f, 0.000f, 0.000f, 1.000f);
    style.Colors[ImGuizmo::DIRECTION_Z] =
        ImVec4(0.000f, 0.666f, 0.000f, 1.000f);
    style.Colors[ImGuizmo::DIRECTION_Y] =
        ImVec4(0.000f, 0.000f, 0.666f, 1.000f);

    ImGui::SetNextWindowPos(ImVec2(pos[0], pos[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(size[0], size[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(96.0f, 96.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    push_utility_window_style(chrome_visible, 0.40f);
    ImGui::Begin(label, NULL, ImGuiWindowFlags_NoCollapse);

    canvas_pos = ImGui::GetCursorScreenPos();
    canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = max(canvas_size.x, 1.0f);
    canvas_size.y = max(canvas_size.y, 1.0f);

    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    mat4_mul(zup2yup, camera->mat, view);
    mat4_invert(view, view);
    mat4_copy(view, view_prev);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(canvas_pos.x, canvas_pos.y,
                      canvas_size.x, canvas_size.y);
    ImGuizmo::ViewManipulate(
           (float*)view, projection,
           ImGuizmo::ROTATE, ImGuizmo::LOCAL,
           (float*)&mat4_identity, camera->dist,
           canvas_pos, canvas_size, 0x0);

    camera_changed = memcmp(view, view_prev, sizeof(view_prev)) != 0;
    if (camera_changed) {
        mat4_invert(view, view);
        mat4_mul(yup2zup, view, camera->mat);
    }

    win_pos = ImGui::GetWindowPos();
    win_size = ImGui::GetWindowSize();
    chrome_visible = utility_window_hovered();
    draw_resize_corners(win_pos, win_size, chrome_visible);
    if (!*pos_set || win_pos.x != pos[0] || win_pos.y != pos[1]) {
        pos[0] = win_pos.x;
        pos[1] = win_pos.y;
        *pos_set = true;
        dirty = true;
    }
    if (!*size_set || win_size.x != size[0] || win_size.y != size[1]) {
        size[0] = win_size.x;
        size[1] = win_size.y;
        *size_set = true;
        dirty = true;
    }

    ImGui::End();
    ImGui::PopStyleColor(9);
    return dirty;
}

bool gui_axis_widget_window(const char *label, float pos[2], float size[2],
                            bool *pos_set, bool *size_set)
{
    static bool chrome_visible = false;
    bool dirty = false;
    float rot[4][4], b[3];
    const float AXIS[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const char *LABELS[] = {"X", "Y", "Z"};
    const ImU32 COLORS[] = {
        IM_COL32(230, 72, 72, 255),
        IM_COL32(64, 210, 95, 255),
        IM_COL32(80, 130, 255, 255),
    };
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList *draw_list;
    ImVec2 canvas_pos;
    ImVec2 canvas_size;
    ImVec2 win_pos;
    ImVec2 win_size;
    ImVec2 center;
    float len;

    if (!*size_set) {
        size[0] = 112.0f;
        size[1] = 112.0f;
    }
    if (!*pos_set) {
        pos[0] = 16.0f;
        pos[1] = max(0.0f, io.DisplaySize.y - size[1] - 16.0f);
    }
    pos[0] = clamp(pos[0], 0.0f, max(0.0f, io.DisplaySize.x - 64.0f));
    pos[1] = clamp(pos[1], 0.0f, max(0.0f, io.DisplaySize.y - 64.0f));
    size[0] = max(size[0], 88.0f);
    size[1] = max(size[1], 88.0f);

    ImGui::SetNextWindowPos(ImVec2(pos[0], pos[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(size[0], size[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(88.0f, 88.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    push_utility_window_style(chrome_visible, 0.40f);
    ImGui::Begin(label, NULL, ImGuiWindowFlags_NoCollapse);

    canvas_pos = ImGui::GetCursorScreenPos();
    canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = max(canvas_size.x, 1.0f);
    canvas_size.y = max(canvas_size.y, 1.0f);
    center = canvas_pos + canvas_size * 0.5f;
    len = min(canvas_size.x, canvas_size.y) * 0.34f;
    draw_list = ImGui::GetWindowDrawList();
    mat4_copy(goxel.image->active_camera->mat, rot);
    vec3_set(rot[3], 0, 0, 0);
    mat4_invert(rot, rot);

    draw_list->AddCircleFilled(center, 3.0f, IM_COL32(230, 230, 230, 220));
    for (int i = 0; i < 3; i++) {
        vec3_mul(AXIS[i], len, b);
        mat4_mul_vec3(rot, b, b);
        ImVec2 end = center + ImVec2(b[0], b[1]);
        draw_list->AddLine(center, end, COLORS[i], 2.0f);
        draw_list->AddCircleFilled(end, 4.0f, COLORS[i]);
        draw_list->AddText(end + ImVec2(5.0f, -6.0f), COLORS[i],
                           LABELS[i]);
    }

    win_pos = ImGui::GetWindowPos();
    win_size = ImGui::GetWindowSize();
    chrome_visible = utility_window_hovered();
    draw_resize_corners(win_pos, win_size, chrome_visible);
    if (!*pos_set || win_pos.x != pos[0] || win_pos.y != pos[1]) {
        pos[0] = win_pos.x;
        pos[1] = win_pos.y;
        *pos_set = true;
        dirty = true;
    }
    if (!*size_set || win_size.x != size[0] || win_size.y != size[1]) {
        size[0] = win_size.x;
        size[1] = win_size.y;
        *size_set = true;
        dirty = true;
    }

    ImGui::End();
    ImGui::PopStyleColor(9);
    return dirty;
}

static bool panel_header_close_button(void)
{
    float w;
    ImVec2 uv0, uv1;
    ImVec2 center;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    bool ret;

    w = gui_get_item_height() + style.FramePadding.x;
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - w, 0));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ret = ImGui::Button("", ImVec2(gui_get_item_height(),
                                   gui_get_item_height()));
    ImGui::PopStyleColor();

    center = ImGui::GetItemRectMin() +
        ImVec2(ImGui::GetItemRectSize().y / 2,
               ImGui::GetItemRectSize().y / 2);
    uv0 = get_icon_uv(ICON_CLOSE);
    uv1 = uv0 + ImVec2(1. / 8, 1. / 8);
    draw_list->AddImage((intptr_t)g_tex_icons->tex,
                            center - ImVec2(12, 12),
                            center + ImVec2(12, 12),
                            uv0, uv1, get_icon_color(ICON_CLOSE, 0));
    return ret;
}

bool gui_panel_header(const char *label)
{
    bool ret;
    float label_w = ImGui::CalcTextSize(label).x;
    float w = ImGui::GetContentRegionAvail().x - gui_get_item_height();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 p1 = ImGui::GetCursorScreenPos();
    ImVec2 p2 = ImVec2(p1.x + ImGui::GetContentRegionAvail().x,
                       p1.y + gui_get_item_height());

    draw_list->AddRectFilled(
            p1, p2,
            ImGui::GetColorU32(color_with_alpha(
                    COLOR(WINDOW, BACKGROUND, false), 1.0f)));

    ImGui::PushID("panel_header");
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2((w - label_w) / 2, 0));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    gui_text(label);
    ret = panel_header_close_button();
    ImGui::EndGroup();
    ImGui::PopID();
    if (ImGui::IsItemHovered())
        gui->can_move_window |= 1;
    return ret;
}

bool gui_icons_grid(int nb, const gui_icon_info_t *icons, int *current)
{
    const gui_icon_info_t *icon;
    char label[128];
    bool v;
    bool ret = false;
    int i;
    float last_button_x;
    float next_button_x;
    float max_x;
    const ImGuiStyle &style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    bool clicked;
    float size;
    bool is_colors_grid;
    float spacing = 2;

    is_colors_grid = (nb > 0 && !icons[0].icon);

    if (is_colors_grid) spacing = 8 * COLOR_SWATCH_SCALE;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

    max_x = ImGui::GetWindowPos().x + ImGui::GetContentRegionAvail().x;
    max_x += 16; // ?

    for (i = 0; i < nb; i++) {
        icon = &icons[i];
        ImGui::PushID(i);
        if (icon->sublabel) {
            snprintf(label, sizeof(label), "%s (%s)",
                     icon->label, icon->sublabel);
        } else {
            snprintf(label, sizeof(label), "%s", icon->label);
        }
        v = (i == *current);
        if (!is_colors_grid) {
            size = GUI_ICON_HEIGHT;
            clicked = gui_selectable_icon(label, &v, icon->icon);
        } else { // Color icon.
            size = gui_get_item_height() * COLOR_SWATCH_SCALE;
            ImGui::PushStyleColor(ImGuiCol_Button, icon->color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, icon->color);
            clicked = ImGui::Button("", ImVec2(size, size));
            ImGui::PopStyleColor(2);
            if (icon->label && ImGui::IsItemHovered())
                gui_tooltip(icon->label);
            if (v) {
                ImVec2 c1 = ImGui::GetItemRectMin() - ImVec2(1, 1);
                ImVec2 c2 = ImGui::GetItemRectMax() + ImVec2(1, 1);
                draw_list->AddRect(c1, c2, 0xFF000000, 0, 0, 2);
                draw_list->AddRect(c1, c2, 0xFFFFFFFF, 0, 0, 1);
            }
        }
        if (clicked) {
            ret = true;
            *current = i;
        }
        last_button_x = ImGui::GetItemRectMax().x;
        next_button_x = last_button_x + style.ItemSpacing.x + size;
        if (i + 1 < nb && next_button_x < max_x)
            ImGui::SameLine();

        ImGui::PopID();

    }
    ImGui::PopStyleVar(1);

    return ret;
}

bool gui_want_capture_mouse(void)
{
    /* Note: we check both gui->want_capture_mouse and
     * io.WantCaptureMouse otherwise there seems to be some subtle input
     * errors on mobile */
    gui_init();
    ImGuiIO& io = ImGui::GetIO();
    return (gui && gui->want_capture_mouse) || io.WantCaptureMouse;
}

bool gui_context_menu_begin(const char *label)
{
    bool ret;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, COLOR(WINDOW, BACKGROUND, false));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, COLOR(WINDOW, INNER, false));

    ret = ImGui::BeginPopupContextItem(label);
    if (ret) {
        gui->is_context_menu = true;
        gui->context_menu_row = gui->is_row;
        gui->is_row = 0;
        gui_group_begin(NULL);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    return ret;
}

void gui_context_menu_end(void)
{
    gui_group_end();
    ImGui::EndPopup();
    gui->is_context_menu = false;
    gui->is_row = gui->context_menu_row;
}

void gui_context_menu_button(const char *label, int icon)
{
    if (gui_button(label, 0, icon)) {
        ImGui::OpenPopup(label);
    }
}

bool gui_is_item_activated(void)
{
    return gui->item_activated;
}

bool gui_is_item_deactivated(void)
{
    return gui->item_deactivated;
}

float gui_get_current_pos_x(void)
{
    return ImGui::GetCursorPosX();
}

void gui_set_current_pos_x(float x)
{
    ImGui::SetCursorPosX(x);
}

void gui_toolbar_begin(void)
{
    gui->toolbar_depth++;
}

void gui_toolbar_end(void)
{
    assert(gui->toolbar_depth > 0);
    gui->toolbar_depth--;
}

float gui_get_item_height(void)
{
    ImGuiStyle& style = ImGui::GetStyle();
    return style.FramePadding.y * 2 + ImGui::GetFontSize();
}

float gui_get_item_spacing_x(void)
{
    return ImGui::GetStyle().ItemSpacing.x;
}

float gui_get_content_width(void)
{
    return ImGui::GetContentRegionAvail().x;
}

float gui_get_color_swatch_size(void)
{
    return color_swatch_size().x;
}

void gui_center_next_items(float width)
{
    float avail = gui_get_content_width();
    if (avail > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) / 2);
}

typedef struct list_item list_item_t;
struct list_item {
    int ret;
    list_item_t *next, *prev;
};

void gui_list(const gui_list_t *list)
{
    list_item_t **items = (list_item_t**)list->items;
    list_item_t *item;
    bool is_current;
    int i;
    int count;

    DL_COUNT(*items, item, count);

    gui_group_begin(NULL);
    i = 0;
    DL_FOREACH(*items, item) {
        is_current = *list->current == item;
        if (list->render((void*)item, i, is_current)) {
            *list->current = item;
            if (is_current && list->can_be_null) {
                *list->current = NULL;
            }
        }
        i++;
    }
    gui_group_end();
}
