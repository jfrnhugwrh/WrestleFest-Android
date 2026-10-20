//
//  Sprite.fsh
//
//  Created by Yen Peng Liao on 2011/3/5.
//  Copyright 2011 Seed Studio. All rights reserved.
//

precision lowp float;

uniform sampler2D   TextureSprite;
uniform sampler2D   TexturePalette;

uniform vec4		SpriteTextureSize;
uniform float		PaletteOffset;
varying vec4        vColor;
varying vec2        vTexcoord;

void main()
{
	float index = texture2D(TextureSprite, vTexcoord).r * 1.999;
	float blank = PaletteOffset - floor(index);
    if (bool(blank))
    {
        gl_FragColor = vec4(0.0);
    }
    else
    {
        gl_FragColor = vColor * texture2D(TexturePalette, vec2(index, 0.5));
    }
}
