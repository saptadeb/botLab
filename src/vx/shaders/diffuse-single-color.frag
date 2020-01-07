#ifdef GL_ES
precision mediump float;
#endif
uniform vec4 color;

varying vec3 color_varying;

void main()
{
    // Calculating The Final Color
    gl_FragColor.rgb = color_varying;

    gl_FragColor[3] = color[3];

}
