#ifdef GL_ES
precision mediump float;
#endif
uniform vec4 color;

void main()
{
    // Calculating The Final Color
    gl_FragColor = color;
}
