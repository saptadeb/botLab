
#ifdef GL_ES
precision mediump float;
#endif

varying  vec2 vtexcoord;
uniform sampler2D texture;
uniform  vec4 color;

void main()
{
	vec4 tcolor = texture2D(texture, vtexcoord);
    gl_FragColor = color;
    gl_FragColor.a *= tcolor.r; // copy an arbitrary channel from this LUMINANCE texture
}
