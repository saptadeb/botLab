#ifdef GL_ES
precision mediump float;
#endif


uniform vec3 light1; // [X Y Z 1]
uniform vec3 light1ambient;
uniform vec3 light1diffuse;
uniform vec3 light1specular;


uniform vec3 light2; // [X Y Z 1]
uniform vec3 light2ambient;
uniform vec3 light2diffuse;
uniform vec3 light2specular;

uniform vec3 view_world; // position of the camera in the world frame

uniform vec3 material_ambient;
uniform vec3 material_diffuse;
uniform vec3 material_specular;

varying vec3 out_normal;
varying vec3 pos_world;

uniform int type;
uniform float specularity;
uniform float transparency;

// L -- lighting direction to object
// E -- eye direction
// R -- reflection
void applyLights(vec3 L, vec3 E, vec3 lt_amb, vec3 lt_diff, vec3 lt_spec, int type)
{
    // blend using

    // ambient

    gl_FragColor.rgb += lt_amb*material_ambient;

    //vec3 L = normalize(lt_pos - pos_world.xyz); // relative location of the light

    // diffuse
    if (type > 0) {
        float v_diffuse = clamp(dot(out_normal, L), 0.0, 1.0);
        gl_FragColor.rgb += material_diffuse*lt_diff*v_diffuse;

        // Supposedly, there's a more efficient way to do specular
        // lighting using the "halway" angle, but I can't get it to look right
        /* if (type > 1 && v_diffuse > 0.0) { */
        /*    vec3 h = (L + E)/2.0; */
        /*    float v_specular = pow(max(0.0, dot(out_normal , h)), 0.4*specularity); */
        /*    gl_FragColor.rgb += material_specular*lt_spec *v_specular; */
        /* } */

        // full specular
        if (type > 1 && v_diffuse > 0.0) {
            vec3 R = normalize(-reflect(L, out_normal));
            float v_specular = pow(max(0.0, dot(R,E)), specularity);//0.3*specularity);
            gl_FragColor.rgb += material_specular*lt_spec *v_specular;
        }
    }
}

void main()
{
    // normalized view direction to fragment
    vec3 v = normalize(view_world - pos_world);

    // normalize light direction to fragment
    vec3 lt1 = normalize(light1-pos_world);
    vec3 lt2 = normalize(light2-pos_world);

    gl_FragColor = vec4(0.0,0.0,0.0,0.0);

    applyLights(lt1, v, light1ambient, light1diffuse, light1specular, type);
    applyLights(lt2, v, light2ambient, light2diffuse, light2specular, type);


    gl_FragColor[3] = transparency;

    // XXX Normal debugging:
    /* gl_FragColor.rgb = abs(out_normal.xyz); */
    /* gl_FragColor[3] = 1.0; */
}

