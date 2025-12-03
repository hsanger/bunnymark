$input a_position, a_texcoord0
$input i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

void main() {
    vec2 position = i_data0.xy;
    vec2 size = i_data0.zw;

    float rotation = i_data1.x;
    // i_data1.yzw are all padding
    float tu = i_data2.x;
    float tv = i_data2.y;
    float tw = i_data2.z;
    float th = i_data2.w;
    vec4 color = i_data3;

    float c = cos(rotation);
    float s = sin(rotation);
    mat2 rotationMat = mat2(c, s, -s, c);
    vec2 basePos = a_position * size;
    vec2 finalPos = position + (rotationMat * basePos);

    gl_Position = mul(u_viewProj, vec4(finalPos, 0.0, 1.0));
    v_texcoord0 = vec2(tu, tv) + (a_texcoord0 * vec2(tw, th));
    v_color0 = color;
}
