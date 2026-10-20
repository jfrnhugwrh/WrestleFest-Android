//
//  Sprite.fsh
//
//  Created by Yen Peng Liao on 2011/3/5.
//  Copyright 2011 Seed Studio. All rights reserved.
//

precision lowp float;

uniform sampler2D   TextureSprite;

varying vec4        vColor;
varying vec2        vTexcoord;

void main()
{
	gl_FragColor = vColor * texture2D(TextureSprite, vTexcoord);
}
