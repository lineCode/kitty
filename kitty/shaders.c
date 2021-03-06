/*
 * shaders.c
 * Copyright (C) 2017 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "gl.h"
#include "fonts.h"
#include <sys/sysctl.h>

enum { CELL_PROGRAM, CELL_BACKGROUND_PROGRAM, CELL_SPECIAL_PROGRAM, CELL_FOREGROUND_PROGRAM, CURSOR_PROGRAM, BORDERS_PROGRAM, GRAPHICS_PROGRAM, NUM_PROGRAMS };
enum { SPRITE_MAP_UNIT, GRAPHICS_UNIT };

// Sprites {{{
typedef struct {
    int xnum, ynum, x, y, z, last_num_of_layers, last_ynum;
    unsigned int cell_width, cell_height;
    GLuint texture_id;
    GLenum texture_unit;
    GLint max_texture_size, max_array_texture_layers;
} SpriteMap;

static SpriteMap sprite_map = { .xnum = 1, .ynum = 1, .last_num_of_layers = 1, .last_ynum = -1, .texture_unit = GL_TEXTURE0 };

static bool copy_image_warned = false;

static void
copy_image_sub_data(GLuint src_texture_id, GLuint dest_texture_id, unsigned int width, unsigned int height, unsigned int num_levels) {
    if (!GLAD_GL_ARB_copy_image) {
        // ARB_copy_image not available, do a slow roundtrip copy
        if (!copy_image_warned) {
            copy_image_warned = true;
            fprintf(stderr, "WARNING: Your system's OpenGL implementation does not have glCopyImageSubData, falling back to a slower implementation.\n");
        }
        uint8_t *src = malloc(5 * width * height * num_levels);
        if (src == NULL) { fatal("Out of memory."); }
        uint8_t *dest = src + (4 * width * height * num_levels);
        glBindTexture(GL_TEXTURE_2D_ARRAY, src_texture_id); 
        glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, src); 
        glBindTexture(GL_TEXTURE_2D_ARRAY, dest_texture_id); 
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 
        for(size_t i = 0; i < width * height * num_levels; i++) dest[i] = src[4*i];
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, num_levels, GL_RED, GL_UNSIGNED_BYTE, dest); 
        free(src);
    } else {
        glCopyImageSubData(src_texture_id, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, dest_texture_id, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, num_levels); 
    }
}


static void
realloc_sprite_texture() {
    GLuint tex;
    glGenTextures(1, &tex); 
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex); 
    // We use GL_NEAREST otherwise glyphs that touch the edge of the cell
    // often show a border between cells
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
    unsigned int xnum, ynum, z, znum, width, height, src_ynum;
    sprite_tracker_current_layout(&xnum, &ynum, &z);
    znum = z + 1;
    width = xnum * sprite_map.cell_width; height = ynum * sprite_map.cell_height;
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_R8, width, height, znum); 
    if (sprite_map.texture_id) {
        // need to re-alloc
        src_ynum = MAX(1, sprite_map.last_ynum);
        copy_image_sub_data(sprite_map.texture_id, tex, width, src_ynum * sprite_map.cell_height, sprite_map.last_num_of_layers);
        glDeleteTextures(1, &sprite_map.texture_id); 
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    sprite_map.last_num_of_layers = znum;
    sprite_map.last_ynum = ynum;
    sprite_map.texture_id = tex;
}

static inline void
ensure_sprite_map() {
    static GLuint bound_texture_id = 0;
    if (!sprite_map.texture_id) realloc_sprite_texture();
    if (bound_texture_id != sprite_map.texture_id) {
        glActiveTexture(GL_TEXTURE0 + SPRITE_MAP_UNIT); 
        glBindTexture(GL_TEXTURE_2D_ARRAY, sprite_map.texture_id); 
        bound_texture_id = sprite_map.texture_id;
    }
}

void 
send_sprite_to_gpu(unsigned int x, unsigned int y, unsigned int z, uint8_t *buf) {
    unsigned int xnum, ynum, znum;
    sprite_tracker_current_layout(&xnum, &ynum, &znum);
    if ((int)znum >= sprite_map.last_num_of_layers || (znum == 0 && (int)ynum > sprite_map.last_ynum)) realloc_sprite_texture();
    glBindTexture(GL_TEXTURE_2D_ARRAY, sprite_map.texture_id); 
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 
    x *= sprite_map.cell_width; y *= sprite_map.cell_height;
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, z, sprite_map.cell_width, sprite_map.cell_height, 1, GL_RED, GL_UNSIGNED_BYTE, buf); 
    Py_DECREF(buf);
}

void
send_image_to_gpu(GLuint *tex_id, const void* data, GLsizei width, GLsizei height, bool is_opaque, bool is_4byte_aligned) {
    if (!(*tex_id)) { glGenTextures(1, tex_id);  }
    glBindTexture(GL_TEXTURE_2D, *tex_id); 
    glPixelStorei(GL_UNPACK_ALIGNMENT, is_4byte_aligned ? 4 : 1); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, is_opaque ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, data);  
}

static bool limits_updated = false;

static void 
layout_sprite_map(unsigned int cell_width, unsigned int cell_height) {
    sprite_map.cell_width = MAX(1, cell_width);
    sprite_map.cell_height = MAX(1, cell_height);
    global_state.cell_width = sprite_map.cell_width;
    global_state.cell_height = sprite_map.cell_height;
    if (!limits_updated) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &(sprite_map.max_texture_size)); 
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &(sprite_map.max_array_texture_layers)); 
        sprite_tracker_set_limits(sprite_map.max_texture_size, sprite_map.max_array_texture_layers);
        limits_updated = true;
    }
    sprite_tracker_set_layout(sprite_map.cell_width, sprite_map.cell_height);
    if (sprite_map.texture_id) { glDeleteTextures(1, &(sprite_map.texture_id)); sprite_map.texture_id = 0; }
    realloc_sprite_texture();
}

static void
destroy_sprite_map() {
    /* sprite_map_free(); */
    if (sprite_map.texture_id) {
        glDeleteTextures(1, &(sprite_map.texture_id));
        sprite_map.texture_id = 0;
    }
}

// }}}

// Cell {{{

typedef struct {
    UniformBlock render_data;
    ArrayInformation color_table;
} CellProgramLayout;

static CellProgramLayout cell_program_layouts[NUM_PROGRAMS];

static void
init_cell_program() {
    for (int i = CELL_PROGRAM; i <= CELL_FOREGROUND_PROGRAM; i++) {
        cell_program_layouts[i].render_data.index = block_index(i, "CellRenderData");
        cell_program_layouts[i].render_data.size = block_size(i, cell_program_layouts[i].render_data.index);
        cell_program_layouts[i].color_table.size = get_uniform_information(i, "color_table[0]", GL_UNIFORM_SIZE);
        cell_program_layouts[i].color_table.offset = get_uniform_information(i, "color_table[0]", GL_UNIFORM_OFFSET);
        cell_program_layouts[i].color_table.stride = get_uniform_information(i, "color_table[0]", GL_UNIFORM_ARRAY_STRIDE);
    }
    // Sanity check to ensure the attribute location binding worked
#define C(p, name, expected) { int aloc = attrib_location(p, #name); if (aloc != expected && aloc != -1) fatal("The attribute location for %s is %d != %d in program: %d", #name, aloc, expected, p); }
    for (int p = CELL_PROGRAM; p <= CELL_FOREGROUND_PROGRAM; p++) {
        C(p, colors, 0); C(p, sprite_coords, 1); C(p, is_selected, 2);
    }
#undef C

}

#define CELL_BUFFERS enum { cell_data_buffer, selection_buffer, uniform_buffer };

static ssize_t
create_cell_vao() {
    ssize_t vao_idx = create_vao();
#define A(name, size, dtype, offset, stride) \
    add_attribute_to_vao(CELL_PROGRAM, vao_idx, #name, \
            /*size=*/size, /*dtype=*/dtype, /*stride=*/stride, /*offset=*/offset, /*divisor=*/1);
#define A1(name, size, dtype, offset) A(name, size, dtype, (void*)(offsetof(Cell, offset)), sizeof(Cell))

    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    A1(sprite_coords, 4, GL_UNSIGNED_SHORT, sprite_x);
    A1(colors, 3, GL_UNSIGNED_INT, fg);

    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    A(is_selected, 1, GL_FLOAT, NULL, 0);

    size_t bufnum = add_buffer_to_vao(vao_idx, GL_UNIFORM_BUFFER);
    alloc_vao_buffer(vao_idx, cell_program_layouts[CELL_PROGRAM].render_data.size, bufnum, GL_STREAM_DRAW);

    return vao_idx;
#undef A
#undef A1
}

static ssize_t
create_graphics_vao() {
    ssize_t vao_idx = create_vao();
    add_buffer_to_vao(vao_idx, GL_ARRAY_BUFFER);
    add_attribute_to_vao(GRAPHICS_PROGRAM, vao_idx, "src", 4, GL_FLOAT, 0, NULL, 0);
    return vao_idx;
}

static inline void
cell_update_uniform_block(ssize_t vao_idx, Screen *screen, int uniform_buffer, GLfloat xstart, GLfloat ystart, GLfloat dx, GLfloat dy, CursorRenderInfo *cursor) {
    struct CellRenderData {
        GLfloat xstart, ystart, dx, dy, sprite_dx, sprite_dy;

        GLuint default_fg, default_bg, highlight_fg, highlight_bg, cursor_color, url_color;

        GLint color1, color2;

        GLuint xnum, ynum, cursor_x, cursor_y, cursor_w, url_xl, url_yl, url_xr, url_yr;
    };
    static struct CellRenderData *rd;

    bool inverted = screen_invert_colors(screen);
    // Send the uniform data
    rd = (struct CellRenderData*)map_vao_buffer(vao_idx, uniform_buffer, GL_WRITE_ONLY);
    if (UNLIKELY(screen->color_profile->dirty)) {
        copy_color_table_to_buffer(screen->color_profile, (GLuint*)rd, cell_program_layouts[CELL_PROGRAM].color_table.offset / sizeof(GLuint), cell_program_layouts[CELL_PROGRAM].color_table.stride / sizeof(GLuint));
    }
    // Cursor position
    if (cursor->is_visible && cursor->shape == CURSOR_BLOCK) { 
        rd->cursor_x = screen->cursor->x, rd->cursor_y = screen->cursor->y; 
    } else {
        rd->cursor_x = screen->columns, rd->cursor_y = screen->lines; 
    }
    rd->cursor_w = rd->cursor_x + MAX(1, screen_current_char_width(screen)) - 1;

    rd->xnum = screen->columns; rd->ynum = screen->lines;
    screen_url_range(screen, &rd->url_xl);
    
    rd->xstart = xstart; rd->ystart = ystart; rd->dx = dx; rd->dy = dy;
    unsigned int x, y, z;
    sprite_tracker_current_layout(&x, &y, &z);
    rd->sprite_dx = 1.0f / (float)x; rd->sprite_dy = 1.0f / (float)y;
    rd->color1 = inverted & 1; rd->color2 = 1 - (inverted & 1);

#define COLOR(name) colorprofile_to_color(screen->color_profile, screen->color_profile->overridden.name, screen->color_profile->configured.name)
    rd->default_fg = COLOR(default_fg); rd->default_bg = COLOR(default_bg); rd->highlight_fg = COLOR(highlight_fg); rd->highlight_bg = COLOR(highlight_bg);
#undef COLOR
    rd->cursor_color = cursor->color; rd->url_color = OPT(url_color);

    unmap_vao_buffer(vao_idx, uniform_buffer); rd = NULL;
}

static inline void
cell_prepare_to_render(ssize_t vao_idx, ssize_t gvao_idx, Screen *screen, GLfloat xstart, GLfloat ystart, GLfloat dx, GLfloat dy, CursorRenderInfo *cursor) {
    size_t sz;
    CELL_BUFFERS;
    void *address;

    ensure_sprite_map();

    if (screen->scroll_changed || screen->is_dirty) {
        sz = sizeof(Cell) * screen->lines * screen->columns;
        address = alloc_and_map_vao_buffer(vao_idx, sz, cell_data_buffer, GL_STREAM_DRAW, GL_WRITE_ONLY);
        screen_update_cell_data(screen, address, sz);
        unmap_vao_buffer(vao_idx, cell_data_buffer); address = NULL;
    }

    if (screen_is_selection_dirty(screen)) {
        sz = sizeof(GLfloat) * screen->lines * screen->columns;
        address = alloc_and_map_vao_buffer(vao_idx, sz, selection_buffer, GL_STREAM_DRAW, GL_WRITE_ONLY);
        screen_apply_selection(screen, address, sz);
        unmap_vao_buffer(vao_idx, selection_buffer); address = NULL;
    }

    if (gvao_idx && grman_update_layers(screen->grman, screen->scrolled_by, xstart, ystart, dx, dy, screen->columns, screen->lines)) {
        sz = sizeof(GLfloat) * 16 * screen->grman->count;
        GLfloat *a = alloc_and_map_vao_buffer(gvao_idx, sz, 0, GL_STREAM_DRAW, GL_WRITE_ONLY);
        for (size_t i = 0; i < screen->grman->count; i++, a += 16) memcpy(a, screen->grman->render_data[i].vertices, sizeof(screen->grman->render_data[0].vertices));
        unmap_vao_buffer(gvao_idx, 0); a = NULL;
    }

    cell_update_uniform_block(vao_idx, screen, uniform_buffer, xstart, ystart, dx, dy, cursor);

    bind_vao_uniform_buffer(vao_idx, uniform_buffer, cell_program_layouts[CELL_PROGRAM].render_data.index);
    bind_vertex_array(vao_idx);
}

static void
draw_graphics(ssize_t vao_idx, ssize_t gvao_idx, ImageRenderData *data, GLuint start, GLuint count) {
    bind_vertex_array(gvao_idx);
    bind_program(GRAPHICS_PROGRAM);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    static bool graphics_constants_set = false;
    if (!graphics_constants_set) { 
        glUniform1i(glGetUniformLocation(program_id(GRAPHICS_PROGRAM), "image"), GRAPHICS_UNIT);  
        graphics_constants_set = true; 
    }
    glActiveTexture(GL_TEXTURE0 + GRAPHICS_UNIT); 

    GLuint base = 4 * start;
    glEnable(GL_SCISSOR_TEST);
    for (GLuint i=0; i < count;) {
        ImageRenderData *rd = data + start + i;
        glBindTexture(GL_TEXTURE_2D, rd->texture_id); 
        // You could reduce the number of draw calls by using
        // glDrawArraysInstancedBaseInstance but Apple chose to abandon OpenGL
        // before implementing it.
        for (GLuint k=0; k < rd->group_count; k++, base += 4, i++) glDrawArrays(GL_TRIANGLE_FAN, base, 4);
    }
    glDisable(GL_SCISSOR_TEST);
    bind_vertex_array(vao_idx);
}

static void
draw_all_cells(ssize_t vao_idx, ssize_t gvao_idx, Screen *screen) {
    bind_program(CELL_PROGRAM); 
    static bool cell_constants_set = false;
    if (!cell_constants_set) { 
        glUniform1i(glGetUniformLocation(program_id(CELL_PROGRAM), "sprites"), SPRITE_MAP_UNIT);  
        cell_constants_set = true; 
    }
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, screen->lines * screen->columns); 
    if (screen->grman->count) draw_graphics(vao_idx, gvao_idx, screen->grman->render_data, 0, screen->grman->count);
}

static void
draw_cells_interleaved(ssize_t vao_idx, ssize_t gvao_idx, Screen *screen) {
    bind_program(CELL_BACKGROUND_PROGRAM); 
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, screen->lines * screen->columns); 

    if (screen->grman->num_of_negative_refs) draw_graphics(vao_idx, gvao_idx, screen->grman->render_data, 0, screen->grman->num_of_negative_refs);

    bind_program(CELL_SPECIAL_PROGRAM); 
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, screen->lines * screen->columns); 

    bind_program(CELL_FOREGROUND_PROGRAM); 
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, screen->lines * screen->columns); 

    if (screen->grman->num_of_positive_refs) draw_graphics(vao_idx, gvao_idx, screen->grman->render_data, screen->grman->num_of_negative_refs, screen->grman->num_of_positive_refs);
}

void 
draw_cells(ssize_t vao_idx, ssize_t gvao_idx, GLfloat xstart, GLfloat ystart, GLfloat dx, GLfloat dy, Screen *screen, CursorRenderInfo *cursor) {
    GLfloat h = (GLfloat)screen->lines * dy;
#define SCALE(w, x) ((GLfloat)(global_state.viewport_##w) * (GLfloat)(x))
    glScissor(
            (GLint)(SCALE(width, (xstart + 1.0f) / 2.0f)), 
            (GLint)(SCALE(height, ((ystart - h) + 1.0f) / 2.0f)),
            (GLsizei)(ceilf(SCALE(width, (float)screen->columns * dx / 2.0f))),
            (GLsizei)(ceilf(SCALE(height, h / 2.0f)))
    );
#undef SCALE
    cell_prepare_to_render(vao_idx, gvao_idx, screen, xstart, ystart, dx, dy, cursor);
    if (screen->grman->num_of_negative_refs) draw_cells_interleaved(vao_idx, gvao_idx, screen);
    else draw_all_cells(vao_idx, gvao_idx, screen);
}
// }}}

// Cursor {{{
enum CursorUniforms { CURSOR_color, CURSOR_pos, NUM_CURSOR_UNIFORMS };
static GLint cursor_uniform_locations[NUM_CURSOR_UNIFORMS] = {0};
static ssize_t cursor_vertex_array;

static void
init_cursor_program() {
    Program *p = programs + CURSOR_PROGRAM;
    int left = NUM_CURSOR_UNIFORMS;
    cursor_vertex_array = create_vao();
    for (int i = 0; i < p->num_of_uniforms; i++, left--) {
#define SET_LOC(which) if (strcmp(p->uniforms[i].name, #which) == 0) cursor_uniform_locations[CURSOR_##which] = p->uniforms[i].location
        SET_LOC(color);
        else SET_LOC(pos);
        else { fatal("Unknown uniform in cursor program"); }
    }
    if (left) { fatal("Left over uniforms in cursor program"); }
#undef SET_LOC
}

void 
draw_cursor(CursorRenderInfo *cursor) {
    bind_program(CURSOR_PROGRAM); bind_vertex_array(cursor_vertex_array); 
    glUniform3f(cursor_uniform_locations[CURSOR_color], ((cursor->color >> 16) & 0xff) / 255.0, ((cursor->color >> 8) & 0xff) / 255.0, (cursor->color & 0xff) / 255.0); 
    glUniform4f(cursor_uniform_locations[CURSOR_pos], cursor->left, cursor->top, cursor->right, cursor->bottom); 
    glDrawArrays(global_state.application_focused ? GL_TRIANGLE_FAN : GL_LINE_LOOP, 0, 4); 
    unbind_vertex_array(); unbind_program();
}
// }}}

// Borders {{{
enum BorderUniforms { BORDER_viewport, NUM_BORDER_UNIFORMS };
static GLint border_uniform_locations[NUM_BORDER_UNIFORMS] = {0};
static ssize_t border_vertex_array;
static GLsizei num_border_rects = 0;
static GLuint rect_buf[5 * 1024];
static GLuint *rect_pos = NULL;

static void
init_borders_program() {
    Program *p = programs + BORDERS_PROGRAM;
    int left = NUM_BORDER_UNIFORMS;
    border_vertex_array = create_vao();
    for (int i = 0; i < p->num_of_uniforms; i++, left--) {
#define SET_LOC(which) if (strcmp(p->uniforms[i].name, #which) == 0) border_uniform_locations[BORDER_##which] = p->uniforms[i].location
        SET_LOC(viewport);
        else { fatal("Unknown uniform in borders program"); return; }
    }
    if (left) { fatal("Left over uniforms in borders program"); return; }
#undef SET_LOC
    add_buffer_to_vao(border_vertex_array, GL_ARRAY_BUFFER);
    add_attribute_to_vao(BORDERS_PROGRAM, border_vertex_array, "rect",
            /*size=*/4, /*dtype=*/GL_UNSIGNED_INT, /*stride=*/sizeof(GLuint)*5, /*offset=*/0, /*divisor=*/1);
    add_attribute_to_vao(BORDERS_PROGRAM, border_vertex_array, "rect_color",
            /*size=*/1, /*dtype=*/GL_UNSIGNED_INT, /*stride=*/sizeof(GLuint)*5, /*offset=*/(void*)(sizeof(GLuint)*4), /*divisor=*/1);
}

void
draw_borders() {
    if (num_border_rects) {
        bind_program(BORDERS_PROGRAM);
        bind_vertex_array(border_vertex_array);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, num_border_rects);
        unbind_vertex_array();
        unbind_program();
    }
}

static void
add_borders_rect(GLuint left, GLuint top, GLuint right, GLuint bottom, GLuint color) {
    if (!left && !top && !right && !bottom) { num_border_rects = 0;  rect_pos = rect_buf; return; }
    num_border_rects++;
    *(rect_pos++) = left;
    *(rect_pos++) = top;
    *(rect_pos++) = right;
    *(rect_pos++) = bottom;
    *(rect_pos++) = color;
}

static void
send_borders_rects(GLuint vw, GLuint vh) {
    if (num_border_rects) {
        size_t sz = sizeof(GLuint) * 5 * num_border_rects;
        void *borders_buf_address = alloc_and_map_vao_buffer(border_vertex_array, sz, 0, GL_STATIC_DRAW, GL_WRITE_ONLY);
        if (borders_buf_address) memcpy(borders_buf_address, rect_buf, sz);
        unmap_vao_buffer(border_vertex_array, 0);
    }
    bind_program(BORDERS_PROGRAM);
    glUniform2ui(border_uniform_locations[BORDER_viewport], vw, vh);
    unbind_program();
}
// }}}

// Python API {{{
static PyObject*
compile_program(PyObject UNUSED *self, PyObject *args) {
    const char *vertex_shader, *fragment_shader;
    int which;
    GLuint vertex_shader_id = 0, fragment_shader_id = 0;
    if (!PyArg_ParseTuple(args, "iss", &which, &vertex_shader, &fragment_shader)) return NULL;
    if (which < 0 || which >= NUM_PROGRAMS) { PyErr_Format(PyExc_ValueError, "Unknown program: %d", which); return NULL; }
    if (programs[which].id != 0) { PyErr_SetString(PyExc_ValueError, "program already compiled"); return NULL; }
    programs[which].id = glCreateProgram();
    vertex_shader_id = compile_shader(GL_VERTEX_SHADER, vertex_shader); 
    fragment_shader_id = compile_shader(GL_FRAGMENT_SHADER, fragment_shader); 
    glAttachShader(programs[which].id, vertex_shader_id); 
    glAttachShader(programs[which].id, fragment_shader_id); 
    glLinkProgram(programs[which].id); 
    GLint ret = GL_FALSE;
    glGetProgramiv(programs[which].id, GL_LINK_STATUS, &ret);
    if (ret != GL_TRUE) {
        GLsizei len;
        glGetProgramInfoLog(programs[which].id, sizeof(glbuf), &len, glbuf);
        fprintf(stderr, "Failed to compile GLSL shader!\n%s", glbuf);
        PyErr_SetString(PyExc_ValueError, "Failed to compile shader");
        goto end;
    }
    init_uniforms(which);

end:
    if (vertex_shader_id != 0) glDeleteShader(vertex_shader_id);
    if (fragment_shader_id != 0) glDeleteShader(fragment_shader_id);
    if (PyErr_Occurred()) { glDeleteProgram(programs[which].id); programs[which].id = 0; return NULL;}
    return Py_BuildValue("I", programs[which].id);
    Py_RETURN_NONE;
}

#define PYWRAP0(name) static PyObject* py##name(PyObject UNUSED *self)
#define PYWRAP1(name) static PyObject* py##name(PyObject UNUSED *self, PyObject *args)
#define PYWRAP2(name) static PyObject* py##name(PyObject UNUSED *self, PyObject *args, PyObject *kw)
#define PA(fmt, ...) if(!PyArg_ParseTuple(args, fmt, __VA_ARGS__)) return NULL;
#define ONE_INT(name) PYWRAP1(name) { name(PyLong_AsSsize_t(args)); Py_RETURN_NONE; } 
#define TWO_INT(name) PYWRAP1(name) { int a, b; PA("ii", &a, &b); name(a, b); Py_RETURN_NONE; } 
#define NO_ARG(name) PYWRAP0(name) { name(); Py_RETURN_NONE; }
#define NO_ARG_INT(name) PYWRAP0(name) { return PyLong_FromSsize_t(name()); }

ONE_INT(bind_program)
NO_ARG(unbind_program)

PYWRAP0(create_vao) {
    int ans = create_vao();
    if (ans < 0) return NULL;
    return Py_BuildValue("i", ans);
}

ONE_INT(remove_vao)

ONE_INT(bind_vertex_array)
NO_ARG(unbind_vertex_array)
TWO_INT(unmap_vao_buffer)

NO_ARG(init_cursor_program)

NO_ARG(init_borders_program)
PYWRAP1(add_borders_rect) { unsigned int a, b, c, d, e; PA("IIIII", &a, &b, &c, &d, &e); add_borders_rect(a, b, c, d, e); Py_RETURN_NONE; }
TWO_INT(send_borders_rects)

NO_ARG(init_cell_program)
NO_ARG_INT(create_cell_vao)
NO_ARG_INT(create_graphics_vao)
NO_ARG(destroy_sprite_map)
PYWRAP1(layout_sprite_map) {
    unsigned int cell_width, cell_height;
    PA("II", &cell_width, &cell_height);
    layout_sprite_map(cell_width, cell_height);
    Py_RETURN_NONE;
}

PYWRAP1(clear_buffers) {
    PyObject *swap_buffers;
    unsigned int bg;
    PA("OI", &swap_buffers, &bg);
#define C(shift) ((float)((bg >> shift) & 0xff)) / 255.0
    glClearColor(C(16), C(8), C(0), 1);
#undef C
    glClear(GL_COLOR_BUFFER_BIT);
    PyObject *ret = PyObject_CallFunctionObjArgs(swap_buffers, NULL);
    if (ret == NULL) return NULL;
    Py_DECREF(ret);
    glClear(GL_COLOR_BUFFER_BIT);
    Py_RETURN_NONE;
}

#define M(name, arg_type) {#name, (PyCFunction)name, arg_type, NULL}
#define MW(name, arg_type) {#name, (PyCFunction)py##name, arg_type, NULL}
static PyMethodDef module_methods[] = {
    M(gl_init, METH_VARARGS),
    M(compile_program, METH_VARARGS),
    MW(create_vao, METH_NOARGS),
    MW(remove_vao, METH_O),
    MW(bind_vertex_array, METH_O),
    MW(unbind_vertex_array, METH_NOARGS),
    MW(unmap_vao_buffer, METH_VARARGS),
    MW(bind_program, METH_O),
    MW(unbind_program, METH_NOARGS),
    MW(init_cursor_program, METH_NOARGS),
    MW(init_borders_program, METH_NOARGS),
    MW(add_borders_rect, METH_VARARGS),
    MW(send_borders_rects, METH_VARARGS),
    MW(init_cell_program, METH_NOARGS),
    MW(create_cell_vao, METH_NOARGS),
    MW(create_graphics_vao, METH_NOARGS),
    MW(layout_sprite_map, METH_VARARGS),
    MW(destroy_sprite_map, METH_NOARGS),
    MW(clear_buffers, METH_VARARGS),

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

bool
init_shaders(PyObject *module) {
#define C(x) if (PyModule_AddIntConstant(module, #x, x) != 0) { PyErr_NoMemory(); return false; }
    C(CELL_PROGRAM); C(CELL_BACKGROUND_PROGRAM); C(CELL_SPECIAL_PROGRAM); C(CELL_FOREGROUND_PROGRAM); C(CURSOR_PROGRAM); C(BORDERS_PROGRAM); C(GRAPHICS_PROGRAM);
    C(GLSL_VERSION);
    C(GL_VERSION);
    C(GL_VENDOR);
    C(GL_SHADING_LANGUAGE_VERSION);
    C(GL_RENDERER);
    C(GL_TRIANGLE_FAN); C(GL_TRIANGLE_STRIP); C(GL_TRIANGLES); C(GL_LINE_LOOP);
    C(GL_COLOR_BUFFER_BIT);
    C(GL_VERTEX_SHADER);
    C(GL_FRAGMENT_SHADER);
    C(GL_TRUE);
    C(GL_FALSE);
    C(GL_COMPILE_STATUS);
    C(GL_LINK_STATUS);
    C(GL_TEXTURE0); C(GL_TEXTURE1); C(GL_TEXTURE2); C(GL_TEXTURE3); C(GL_TEXTURE4); C(GL_TEXTURE5); C(GL_TEXTURE6); C(GL_TEXTURE7); C(GL_TEXTURE8);
    C(GL_MAX_ARRAY_TEXTURE_LAYERS); C(GL_TEXTURE_BINDING_BUFFER); C(GL_MAX_TEXTURE_BUFFER_SIZE);
    C(GL_MAX_TEXTURE_SIZE);
    C(GL_TEXTURE_2D_ARRAY);
    C(GL_LINEAR); C(GL_CLAMP_TO_EDGE); C(GL_NEAREST);
    C(GL_TEXTURE_MIN_FILTER); C(GL_TEXTURE_MAG_FILTER);
    C(GL_TEXTURE_WRAP_S); C(GL_TEXTURE_WRAP_T);
    C(GL_UNPACK_ALIGNMENT);
    C(GL_R8); C(GL_RED); C(GL_UNSIGNED_BYTE); C(GL_UNSIGNED_SHORT); C(GL_R32UI); C(GL_RGB32UI); C(GL_RGBA);
    C(GL_TEXTURE_BUFFER); C(GL_STATIC_DRAW); C(GL_STREAM_DRAW); C(GL_DYNAMIC_DRAW);
    C(GL_SRC_ALPHA); C(GL_ONE_MINUS_SRC_ALPHA); 
    C(GL_WRITE_ONLY); C(GL_READ_ONLY); C(GL_READ_WRITE);
    C(GL_BLEND); C(GL_FLOAT); C(GL_UNSIGNED_INT); C(GL_ARRAY_BUFFER); C(GL_UNIFORM_BUFFER);

#undef C
    PyModule_AddObject(module, "GL_VERSION_REQUIRED", Py_BuildValue("II", REQUIRED_VERSION_MAJOR, REQUIRED_VERSION_MINOR));
    if (PyModule_AddFunctions(module, module_methods) != 0) return false;
    return true;
}
// }}}
