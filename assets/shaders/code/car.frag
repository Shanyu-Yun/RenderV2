#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

struct GpuLight
{
    vec4 position;
    vec4 direction;
    vec4 colorIntensity;
    vec4 spotParams;
};

layout(set = 0, binding = 1) uniform LightData
{
    GpuLight lights[16];
    uint lightCount;
} uLight;

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;

void main()
{
    vec3 normal = normalize(fragNormal);
    GpuLight light = GpuLight(vec4(0.0), vec4(0.0, -1.0, 0.0, 1.0), vec4(1.0), vec4(0.0));
    if (uLight.lightCount > 0)
    {
        light = uLight.lights[0];
    }
    vec3 lightDir = normalize(-light.direction.xyz);
    float ndotl = max(dot(normal, lightDir), 0.0);

    vec3 albedo = texture(baseColorTex, fragUV).rgb;
    vec3 diffuse = albedo * light.colorIntensity.rgb * ndotl * light.colorIntensity.w;

    // 简单的环境光
    vec3 ambient = albedo * 0.1;

    outColor = vec4(diffuse + ambient, 1.0);
}
