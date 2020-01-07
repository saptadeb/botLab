uniform vec3 view_world; // position of the camera in the world frame

uniform mat4 PM; // Proj*Model Required by our interface
uniform mat4 M; // Model matrix
uniform mat3 N; // Normal matrix = (M^-1)'
uniform float pointSize;

attribute vec4 position;
attribute vec3 normal;

varying vec3 out_normal;
varying vec3 pos_world;

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;

    gl_PointSize = pointSize;
    out_normal = normalize(N*normal);
    pos_world = vec3(M*position);

}
