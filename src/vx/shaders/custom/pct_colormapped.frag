#ifdef GL_ES
precision mediump float;
#endif
varying vec4 cls;

void main()
{
    // Calculating The Final Color
    gl_FragColor = cls;
}
