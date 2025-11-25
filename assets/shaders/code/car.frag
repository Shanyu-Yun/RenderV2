#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in mat3 vTBN;

layout(location = 0) out vec4 outColor;

struct GpuLight {
    vec4 position;
    vec4 direction;
    vec4 colorIntensity;
    vec4 spotParams;
};

layout(set = 0, binding = 0) uniform LightUBO
{
    GpuLight lights[16];
    uint lightCount;
}
uLights;

layout(set = 0, binding = 1) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    vec4 viewPosition;
}
uCamera;

//
layout(set = 1, binding = 0) uniform sampler2D uBaseColorMap;
layout(set = 1, binding = 1) uniform sampler2D uNormalMap;
layout(set = 1, binding = 2) uniform sampler2D uMetallicMap;
layout(set = 1, binding = 3) uniform sampler2D uRoughnessMap;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
        * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

vec3 getWorldNormal()
{
    vec3 nTex = texture(uNormalMap, vTexCoord).xyz * 2.0 - 1.0;
    return normalize(vTBN * nTex);
}

void main()
{
    vec3 albedo = pow(texture(uBaseColorMap, vTexCoord).rgb, vec3(2.2)); // sRGB → Linear
    float metallic = texture(uMetallicMap, vTexCoord).r;
    float roughness = max(texture(uRoughnessMap, vTexCoord).r, 0.04);

    vec3 N = getWorldNormal();
    vec3 V = normalize(uCamera.viewPosition.xyz - vWorldPos);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    for (uint i = 0u; i < uLights.lightCount; ++i) {
        GpuLight light = uLights.lights[i];

        vec3 L = normalize(light.position.xyz - vWorldPos);
        float dist = length(light.position.xyz - vWorldPos);
        float attenuation = 1.0 / (dist * dist);
        vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;

        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        vec3 diffuse = kD * albedo / PI;
        vec3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);

        Lo += (diffuse + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03) * albedo;
    vec3 color = ambient + Lo;

    color = pow(color, vec3(1.0 / 2.2)); // Linear → sRGB

    outColor = vec4(color, 1.0);
}
