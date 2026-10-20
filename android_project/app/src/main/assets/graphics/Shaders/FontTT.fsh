/* 
  Font.fsh
  iPhone01_WaterPressure

  Created by Yen Peng Liao on 2011/3/18.
  Copyright 2011 Seed Studio. All rights reserved.
*/

precision lowp float;

uniform sampler2D   TextureSprite;

varying vec4        vColor;
varying vec2        vTexcoord;

void main()
{
    float tex = texture2D(TextureSprite, vTexcoord).r;
	gl_FragColor = vec4(vColor.rgb, tex * vColor.a);
}
