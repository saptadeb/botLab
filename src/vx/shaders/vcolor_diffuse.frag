#ifdef GL_ES
precision mediump float;
#endif

varying vec4 color_varying;

void main()
{
    // Calculating The Final Color
    gl_FragColor = color_varying;

}
