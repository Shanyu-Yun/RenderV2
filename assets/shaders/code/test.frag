#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// Set 1 - Material + textures
layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex[10];
layout(set = 1, binding = 2) uniform samplerCube envMap;

// Set 2 - Per-frame attachments (input attachment test)
layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput colorInput;

// Push constant block reused
layout(push_constant) uniform PushConsts
{
    vec4 colorMod;
    float metallic;
    float roughness;
}
pushConsts;

void main()
{
    vec3 N = normalize(vNormal);
    vec4 texC = texture(baseColorTex, vUV) * pushConsts.colorMod;
    vec3 env = texture(envMap, N).rgb * pushConsts.metallic;
    vec3 fromSubpass = subpassLoad(colorInput).rgb;

    outColor = vec4(texC.rgb * 0.5 + env * 0.3 + fromSubpass * 0.2, texC.a);
}
