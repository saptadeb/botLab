
uniform mat4 PM; // Required by our interface

attribute vec4 position;

attribute vec4 texcoord;
varying  vec2 vtexcoord;

void main()
{
    gl_Position = PM*position;

    vtexcoord = texcoord.xy;
}
