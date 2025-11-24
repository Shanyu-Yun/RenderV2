#version 450
#extension GL_ARB_separate_shader_objects : enable
#pragma shader_stage(fragment)

// 从顶点着色器输入
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

// 输出颜色
layout(location = 0) out vec4 outColor;

// === Set 0: 全局渲染数据 (每帧更新) ===
layout(set = 0, binding = 0) uniform GlobalUniforms
{
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec3 cameraPosition;
    float time;
    vec3 lightDirection;
    float padding1;
    vec3 lightColor;
    float lightIntensity;
}
global;

// === Set 1: 材质数据 (按材质更新) ===
layout(set = 1, binding = 0) uniform MaterialUniforms
{
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float alphaCutoff;
    float padding2;
}
material;

// 材质纹理
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicTexture;
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D normalTexture;
layout(set = 1, binding = 5) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 6) uniform sampler2D emissiveTexture;

// === Set 2: 物体变换数据 (按物体更新) ===
layout(set = 2, binding = 0) uniform ObjectUniforms
{
    mat4 modelMatrix;
    mat4 normalMatrix;
}
object;

// 简单的PBR计算
vec3 calculatePBR(vec3 albedo, float metallic, float roughness, vec3 normal, vec3 viewDir, vec3 lightDir)
{
    // 简化的PBR实现
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = albedo * (1.0 - metallic) * NdotL;

    // 简单的镜面反射
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float spec = pow(NdotH, (1.0 - roughness) * 256.0);
    vec3 specular = mix(vec3(0.04), albedo, metallic) * spec;

    return diffuse + specular;
}

void main()
{
    // 采样材质纹理
    vec4 baseColor = texture(baseColorTexture, fragTexCoord) * material.baseColorFactor;
    float metallic = texture(metallicTexture, fragTexCoord).r * material.metallicFactor;
    float roughness = texture(roughnessTexture, fragTexCoord).r * material.roughnessFactor;
    vec3 normal = normalize(fragNormal); // 简化，不处理法线贴图

    // 计算视线方向
    vec3 viewDir = normalize(global.cameraPosition - fragWorldPos);

    // PBR光照计算
    vec3 color = calculatePBR(baseColor.rgb, metallic, roughness, normal, viewDir, global.lightDirection);

    // 添加自发光
    vec3 emissive = texture(emissiveTexture, fragTexCoord).rgb * material.emissiveFactor;
    color += emissive;

    outColor = vec4(color, baseColor.a);
}