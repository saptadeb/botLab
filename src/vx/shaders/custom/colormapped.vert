
uniform mat4 PM; // Required by our interface
uniform mat4 M; // Model matrix

uniform float pointSize;

attribute vec4 position;

varying vec4 vcolor;

uniform int cm_axis;
uniform float cm_min;
uniform float cm_max;

// GLSL only allows const size uniform arrays.
//  TODO: add vx class to programmatically generate these with different
//  'count'
const int cm_count = 9;
uniform vec4 cm_map[cm_count];

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;
    vec4 world_pos = M * position;

    gl_PointSize = pointSize;

    float value = world_pos[cm_axis];

    float normval = float(cm_count - 1) * (value - cm_min) / (cm_max - cm_min);
    normval = clamp(normval, 0.0, float(cm_count -1));

    int a = int(floor(normval));
    int b = a + 1;

    float frac = normval - float(a);

    vcolor = cm_map[a] * (1.0-frac) + cm_map[b]*frac;
}
