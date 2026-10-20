//
//  Blit.vsh
//
//  Created by Yen Peng Liao on 2011/3/5.
//  Copyright 2011 Seed Studio. All rights reserved.
//

uniform     vec4 ViewportDimension;

attribute   vec4 tyPosition;
attribute   vec4 tyColor;
attribute   vec2 tyTexcoord;

varying     vec4 vColor;
varying     vec2 vTexcoord;

void main()
{
    vec2 pos = tyPosition.xy - ViewportDimension.zw;
    pos /= ViewportDimension.zw;
    
	vColor = tyColor;
	vTexcoord = tyTexcoord;
    
	gl_Position = vec4(pos, tyPosition.zw);
}
