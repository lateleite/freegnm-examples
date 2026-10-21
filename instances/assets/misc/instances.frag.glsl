#version 450
#extension GL_ARB_separate_shader_objects : enable

struct PointLight
{
	vec3 Position;
	float Falloff;
	vec3 Color;
	float ColorIntensity;
};
// HACK: psbc needs the binding to be manually specified in order
// to generate correct resource offsets for now
layout(set = 0, binding = 12) uniform Constants {
	mat4 mproj;
	mat4 mview;
	mat4 mmodel;
	vec3 viewpos;
	uint numlights;
	PointLight lights[3];
} c;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec3 innorm;
layout(location = 1) in vec2 inuv;
layout(location = 2) in vec3 inposview;
layout(location = 3) in vec2 inmetallic_roughness;

layout(location = 0) out vec4 outcolor;

const float PI = 3.14159265359;

float D_GGX(float NoH, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float f = NoH * NoH * (a2 - 1.0) + 1.0;
	return a2 / (PI * f * f);
}

float V_SchlicksmithGGX(float NoL, float NoV, float roughness) {
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;
	float GL = NoL / (NoL * (1.0 - k) + k);
	float GV = NoV / (NoV * (1.0 - k) + k);
	return GL * GV;
}

vec3 F_Schlick(vec3 F0, float cosTheta) {
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float Fd_Lambert() {
	return 1.0 / PI;
}

vec3 SpecularBRDF(
	float NoH, float NoL, float NoV, vec3 F0,
	float roughness
) {
	float D = D_GGX(NoH, roughness);
	float G = V_SchlicksmithGGX(NoL, NoV, roughness);
	vec3 F = F_Schlick(F0, NoV);

	return D * G * F / (4.0 * NoL * NoV);
}

vec3 DiffuseBRDF(vec3 albedoColor) {
	return albedoColor * Fd_Lambert();
}

// attenuation from frostbite
float SmoothDistanceAtt(float squaredDistance, float invSqrAttRadius)
{
    float factor = squaredDistance * invSqrAttRadius;
    float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
    return smoothFactor * smoothFactor;
}

float FrostbiteDistanceAttenuation(
	const vec3 unormLightVector, float invSqrAttRadius
) {
	float sqrDist = dot(unormLightVector, unormLightVector);

	float attenuation =
		1.0 / (max(sqrDist, 0.01 * 0.01));
	attenuation *= SmoothDistanceAtt(sqrDist, invSqrAttRadius);

	return attenuation;
}

vec3 BRDF(
	vec3 V, vec3 N, vec3 worldPos, vec3 fragColor,
	vec3 F0, float metallic, float roughness, PointLight light
) {
	vec3 lightWorldUnorm = light.Position - worldPos;
	vec3 L = normalize(lightWorldUnorm);

	// Precalculate floattors and dot products
	vec3 H = normalize(V + L);
	float NoV = clamp(dot(N, V), 0.0, 1.0);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);
	float LoH = clamp(dot(L, H), 0.0, 1.0);

	float attenuation =
		FrostbiteDistanceAttenuation(lightWorldUnorm, light.Falloff);

	if (NoL <= 0.0 || attenuation <= 0.0) {
		return vec3(0.0);
	}

	float colorIntensity = light.ColorIntensity;

	vec3 F = F_Schlick(F0, NoV);

	vec3 kD = 1.0 - F;
	kD *= 1.0 - metallic;

	vec3 specular = SpecularBRDF(NoH, NoL, NoV, F0, roughness);
	vec3 diffuse = DiffuseBRDF(kD * fragColor);

	vec3 color = specular + diffuse;

	return (color * light.Color) * (colorIntensity * attenuation * NoL);
}

vec3 F_SchlickR(float cosTheta, vec3 F0, float roughness) {
	return F0 + (max(vec3(1.0 - roughness), F0) - F0)
		* pow(1.0 - cosTheta, 5.0);
}

vec3 RgbToLinear(const vec3 rgbColor, const float gamma)
{
	return pow(rgbColor, vec3(gamma));
}

vec3 LinearToRgb(const vec3 linearColor, const float gamma)
{
	return pow(linearColor, vec3(1.0 / gamma));
}

void main() {
	vec3 albedo = texture(tex, inuv).xyz;

	vec3 V = normalize(c.viewpos - inposview);
	vec3 N = normalize(innorm);

	float metallic = inmetallic_roughness.x;
	float roughness = inmetallic_roughness.y;

	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	// specular + diffuse BRDF
	vec3 Lo = vec3(0.0);
	for (uint i = 0; i < c.numlights; i += 1)
	{
		Lo += BRDF(
			V, N, inposview, albedo, F0, metallic,
			roughness, c.lights[i] 
		);
	};

	// diffuse
	vec3 ambient = albedo * 0.02;

	vec3 color = ambient + Lo;

	// Gamma correct
	color = LinearToRgb(color, 2.2);

	outcolor = vec4(color, 1.0);
}
