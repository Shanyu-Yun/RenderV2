#version 450
#extension GL_ARB_separate_shader_objects : enable

// 从顶点着色器输入
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragColor;

// 输出颜色
layout(location = 0) out vec4 outColor;

// 纹理采样器
layout(binding = 0) uniform sampler2D texSampler;

void main()
{
    // 简单的漫反射着色
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 norm = normalize(fragNormal);
    float diff = max(dot(norm, lightDir), 0.0);

    // 采样纹理
    vec4 texColor = texture(texSampler, fragTexCoord);

    // 结合光照、纹理和顶点色
    vec3 ambient = 0.3 * texColor.rgb;
    vec3 diffuse = diff * texColor.rgb;

    outColor = vec4(ambient + diffuse, texColor.a);
}