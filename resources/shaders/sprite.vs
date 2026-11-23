layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

#include "engine://shaders/camera_data.glsl"

out vec2 TexCoords;
uniform mat4 uModel;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
    TexCoords = aTexCoord;
}
