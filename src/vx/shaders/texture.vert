uniform mat4 PM; // Required by our interface

attribute vec4 position;

attribute vec2 texIn;
varying vec2 texOut;


void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;

    texOut = texIn; // will get interpolated
}
