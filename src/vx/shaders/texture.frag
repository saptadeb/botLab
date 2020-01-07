#ifdef GL_ES
precision mediump float;
#endif
varying vec4 cls;

uniform sampler2D texture;

varying vec2 texOut;
void main()
{

    // Calculating The Final Color
    gl_FragColor = texture2D(texture, texOut);
}
