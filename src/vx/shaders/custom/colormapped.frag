#ifdef GL_ES
precision mediump float;
#endif

varying vec4 vcolor;

void main()
{
    // Calculating The Final Color
    gl_FragColor = vcolor;
}
