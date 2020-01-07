uniform mat4 PM; // Proj*Model Required by our interface
uniform mat4 M; // Model matrix
uniform mat3 N; // Normal matrix = (M^-1)'
uniform float pointSize;

attribute vec4 position;
attribute vec3 normal;

#ifdef GL_ES
uniform mediump vec4 color; // rgb a
#else
uniform vec4 color; // rgb a
#endif


varying vec3 color_varying;


uniform vec3 light1; // [X Y Z 1]
uniform vec3 light1ambient;
uniform vec3 light1diffuse;
uniform vec3 light1specular;


uniform vec3 light2; // [X Y Z 1]
uniform vec3 light2ambient;
uniform vec3 light2diffuse;
uniform vec3 light2specular;


void applyLights(vec3 L, vec3 out_normal, vec3 lt_amb, vec3 lt_diff)//, vec3 E,  vec3 lt_spec, int type)
{

    color_varying.rgb += lt_amb*color.rgb;

    float v_diffuse = clamp(dot(out_normal, L), 0.0, 1.0);
    color_varying.rgb += color.rgb*lt_diff*v_diffuse;

    /* if (v_diffuse > 0.0) { */
    /*     vec3 R = normalize(-reflect(L, out_normal)); */
    /*     float v_specular = pow(max(0.0, dot(R,E)), specularity);//0.3*specularity); */
    /*     color_varying.rgb += color*lt_spec *v_specular; */
    /* } */
}

void main()
{
    // Transforming the Vertex
    gl_Position = PM*position;

    gl_PointSize = pointSize;
    vec3 out_normal = normalize(N*normal);
    vec3 pos_world = vec3(M*position);

    // Compute per vertex lighting:
    vec3 lt1 = normalize(light1-pos_world);
    vec3 lt2 = normalize(light2-pos_world);


    color_varying = vec3(0.0,0.0,0.0);

    applyLights(lt1, out_normal, light1ambient, light1diffuse);
    applyLights(lt2, out_normal, light2ambient, light2diffuse);

}
