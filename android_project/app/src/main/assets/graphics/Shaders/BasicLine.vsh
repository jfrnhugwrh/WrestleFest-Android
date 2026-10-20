//
//  BasicLine.vsh
//  iPhone01_WaterPressure
//
//  Created by Yen Peng Liao on 2011/3/5.
//  Copyright 2011 Seed Studio. All rights reserved.
//

attribute vec4 tyPosition;
attribute vec4 tyColor;

uniform mat4 ViewProjection;
uniform mat4 World;

varying vec4 vColor;

void main()
{
	vColor = tyColor;
	gl_Position = tyPosition * World * ViewProjection;
}

