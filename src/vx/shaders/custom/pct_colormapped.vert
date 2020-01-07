uniform mat4 PM; // Required by our interface
uniform mat4 M; // Model matrix

uniform int type;
uniform float pointSize;

attribute vec4 position;
attribute vec4 color;

uniform vec3 view_world; // position of the camera in the world frame

varying vec4 cls;

void main()
{
    // Transforming The Vertex
    gl_Position = PM*position;

    vec3 pos_world = vec3(M*position);

    float pt_dist = distance(pos_world, view_world);


    cls = color;


    if (type == 1) {
        gl_PointSize =  pointSize;
    } else if (type == 2) {
        gl_PointSize =  pointSize/pt_dist;
    } else {
        gl_PointSize =  10.0;
    }
}
