uniform mat4 PM; // Required by our interface
uniform float pointSize;

attribute vec4 position;
attribute vec4 color;

varying vec4 cls;

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;
    cls = color;
    gl_PointSize = pointSize;
}
