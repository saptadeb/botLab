precision mediump float;

uniform mat4 PM; // Required by our interface

uniform float pointSize;
uniform float color_mode;
uniform float colormap[3*128];
uniform float z_off;
uniform float z_width;

attribute vec4 position;
attribute float intensity;

varying vec4 cls;

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;
    gl_PointSize = pointSize;

    float z = vertex[2] - z_off;
    float z_norm = (z - z_width/2.0) / z_width;
    if (z_norm > 1.0)
           z_norm = 1.0;
    if (z_norm < 0.0)
            z_norm = 0.0;
    int idx = (127 * z_norm);
    vec4 height_color = vec4 (colormap_r[idx], colormap_g[idx], colormap_b[idx], 1.0);

    if (color_mode == 0) // by z
        cls  = height_color;
    else if (color_mode == 1) { // by intensity
        vec4 intensity_color;
        if (intensity < 0.396) {
            float tmp = intensity*2.55;
            intensity_color = vec4(tmp, tmp, tmp, 1.0);
        } else {
            intensity_color = vec4(intensity, 0.0, 0.0, 1.0);
        }
        cls  = intensity_color;
    } else if (color_mode == 2) { // intensity/height blend
        vec4 blended_color;
        if (intensity < 0.396) {
            float tmp = intensity*2.55;
            vec4 icolor = vec4 (tmp, tmp, tmp, 1.0);
            blended_color = height_color*alpha + icolor*(1.0-alpha);
        } else {
            blended_color = vec4 (1.0, 0.0, 1.0, 1.0);
        }
        cls  = blended_color;
    }
}
