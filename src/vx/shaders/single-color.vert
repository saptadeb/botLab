uniform mat4 PM; // Required by our interface
uniform float pointSize;

attribute vec4 position;

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;
    gl_PointSize = pointSize;
}
