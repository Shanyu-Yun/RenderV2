#version 450
#extension GL_ARB_separate_shader_objects : enable

// 顶点输入 - 按当前管线配置的顺序
layout(location = 0) in vec3 inPosition; // location 0
layout(location = 1) in vec3 inNormal; // location 1
layout(location = 2) in vec2 inTexCoord; // location 2
layout(location = 3) in vec4 inColor; // location 3

// 输出到片段着色器
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragColor;

void main()
{
    // 简单的正交投影，将模型放在屏幕中央并缩放
    vec3 scaledPos = inPosition * 0.1; // 缩小模型
    gl_Position = vec4(scaledPos.xy, 0.5, 1.0); // Z = 0.5 使其在屏幕中间

    fragTexCoord = inTexCoord;
    fragNormal = inNormal;
    fragColor = inColor;
}