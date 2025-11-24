#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragWorldPos;

layout(set = 0, binding = 0) uniform CameraData
{
    mat4 view;
    mat4 projection;
    vec4 viewPosition;
} uCamera;

void main()
{
    vec4 worldPos = vec4(inPosition, 1.0);
    gl_Position = uCamera.projection * uCamera.view * worldPos;
    fragNormal = inNormal;
    fragUV = inTexCoord;
    fragWorldPos = worldPos.xyz;
}
