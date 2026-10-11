layout (std140) uniform FillDrawableUBO {
    highp mat4 u_matrix;
    // Interpolations
    highp float u_color_t;
    highp float u_opacity_t;
    lowp float u_tile_fade;
    lowp float drawable_pad2;
};

layout (std140) uniform FillEvaluatedPropsUBO {
    highp vec4 u_color;
    highp vec4 u_outline_color;
    highp float u_opacity;
    highp float u_fade;
    highp float u_from_scale;
    highp float u_to_scale;
};

layout (location = 0) in vec2 a_pos;

#pragma mapbox: define highp vec4 color
#pragma mapbox: define lowp float opacity

out lowp float v_tile_fade;

void main() {
    v_tile_fade = u_tile_fade;
    #pragma mapbox: initialize highp vec4 color
    #pragma mapbox: initialize lowp float opacity

    gl_Position = projectTile(a_pos, a_pos);
}
