#version GLSL_VERSION
#define WHICH_PROGRAM
layout(std140) uniform CellRenderData {
    float xstart, ystart, dx, dy, sprite_dx, sprite_dy;

    uint default_fg, default_bg, highlight_fg, highlight_bg, cursor_color, url_color;

    int color1, color2;

    uint xnum, ynum, cursor_x, cursor_y, cursor_w, url_xl, url_yl, url_xr, url_yr;

    uint color_table[256]; 
};

// Have to use fixed locations here as all variants of the cell program share the same VAO
layout(location=0) in uvec3 colors;
layout(location=1) in uvec4 sprite_coords;
layout(location=2) in float is_selected;

#if defined(FOREGROUND) || defined(ALL)

out vec3 sprite_pos;
out vec3 underline_pos;
out vec3 strike_pos;
out vec3 foreground;
out vec3 decoration_fg;
#endif
out vec3 background;
#ifdef SPECIAL
out vec4 special_bg;
#endif

const uvec2 pos_map[] = uvec2[4](
    uvec2(1, 0),  // right, top
    uvec2(1, 1),  // right, bottom
    uvec2(0, 1),  // left, bottom
    uvec2(0, 0)   // left, top
);

const uint BYTE_MASK = uint(0xFF);
const uint SHORT_MASK = uint(0xFFFF);
const uint ZERO = uint(0);
const uint ONE = uint(1);
const uint TWO = uint(2);
const uint THREE = uint(3);
const uint DECORATION_MASK = uint(3);
const uint STRIKE_MASK = uint(1);
const uint REVERSE_MASK = uint(1);

vec3 color_to_vec(uint c) {
    uint r, g, b;
    r = (c >> 16) & BYTE_MASK;
    g = (c >> 8) & BYTE_MASK;
    b = c & BYTE_MASK;
    return vec3(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0);
}

uint resolve_color(uint c, uint defval) {
    // Convert a cell color to an actual color based on the color table
    int t = int(c & BYTE_MASK);
    uint r;
    switch(t) {
        case 1:
            r = color_table[(c >> 8) & BYTE_MASK];
            break;
        case 2:
            r = c >> 8;
            break;
        default:
            r = defval;
    }
    return r;
}

vec3 to_color(uint c, uint defval) {
    return color_to_vec(resolve_color(c, defval));
}

vec3 to_sprite_pos(uvec2 pos, uint x, uint y, uint z) {
    vec2 s_xpos = vec2(x, float(x) + 1.0) * sprite_dx;
    vec2 s_ypos = vec2(y, float(y) + 1.0) * sprite_dy;
    return vec3(s_xpos[pos.x], s_ypos[pos.y], z);
}

vec3 choose_color(float q, vec3 a, vec3 b) {
    return q * a + (1.0 - q) * b;
}

float in_range(uint x, uint y) {
    if (url_yl == y && url_xl <= x && x <= url_xr) return 1.0;
    return 0.0;
}

float is_cursor(uint x, uint y) {
    if (y == cursor_y && (x == cursor_x || x == cursor_w)) return 1.0;
    return 0.0;
}

void main() {
    float cursor;
    uint instance_id = uint(gl_InstanceID);
    // The current cell being rendered
    uint r = instance_id / xnum;
    uint c = instance_id - r * xnum;

    // The position of this vertex, at a corner of the cell
    float left = xstart + c * dx;
    float top = ystart - r * dy;
    vec2 xpos = vec2(left, left + dx);
    vec2 ypos = vec2(top, top - dy);
    uvec2 pos = pos_map[gl_VertexID];
    gl_Position = vec4(xpos[pos.x], ypos[pos.y], 0, 1);

    uvec2 default_colors = uvec2(default_fg, default_bg);
    ivec2 color_indices = ivec2(color1, color2);

    uint text_attrs = sprite_coords[3];
    int fg_index = color_indices[(text_attrs >> 6) & REVERSE_MASK];
    int bg_index = color_indices[1 - fg_index];
    background = to_color(colors[bg_index], default_colors[bg_index]);

#if defined(FOREGROUND) || defined(ALL)
    // The character sprite being rendered
    sprite_pos = to_sprite_pos(pos, sprite_coords.x, sprite_coords.y, sprite_coords.z & SHORT_MASK);

    // Foreground and background colors
    uint resolved_fg = resolve_color(colors[fg_index], default_colors[fg_index]);
    foreground = color_to_vec(resolved_fg);
    // Selection
    foreground = choose_color(is_selected, color_to_vec(highlight_fg), foreground);
    // Underline and strike through (rendered via sprites)
    float in_url = in_range(c, r);
    decoration_fg = choose_color(in_url, color_to_vec(url_color), to_color(colors[2], resolved_fg));
    underline_pos = choose_color(in_url, to_sprite_pos(pos, TWO, ZERO, ZERO), to_sprite_pos(pos, (text_attrs >> 2) & DECORATION_MASK, ZERO, ZERO));
    strike_pos = to_sprite_pos(pos, ((text_attrs >> 7) & STRIKE_MASK) * THREE, ZERO, ZERO);
    // Block cursor rendering
    cursor = is_cursor(c, r);
    foreground = choose_color(cursor, background, foreground);
    decoration_fg = choose_color(cursor, background, decoration_fg);
#endif

#if defined(ALL)
    // Selection and cursor
    background = choose_color(is_selected, color_to_vec(highlight_bg), background);
    background = choose_color(cursor, color_to_vec(cursor_color), background);
#elif defined(SPECIAL)
    cursor = is_cursor(c, r);
    background = choose_color(is_selected, color_to_vec(highlight_bg), background);
    background = choose_color(cursor, color_to_vec(cursor_color), background);
    special_bg = vec4(background, max(cursor, is_selected));
#endif
}
