#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out mat3 vTBN;

layout(set = 0, binding = 1) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    vec4 viewPosition;
}
uCamera;

void main()
{
    vec4 worldPos = vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;

    mat3 normalMat = transpose(inverse(mat3(1.0))); // Assuming model matrix is identity
    vec3 N = normalize(normalMat * inNormal);

    // Auto-generate tangent in shader if model vertex has no tangent
    // We build a simple orthonormal basis around N
    vec3 helper = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 T = normalize(cross(helper, N));
    vec3 B = normalize(cross(N, T));

    vTBN = mat3(T, B, N);

    vNormal = N;
    vTexCoord = inTexCoord;

    gl_Position = uCamera.projection * uCamera.view * worldPos;
}
