// ==========================================
// DS.hlsl Ч Domain Shader (geometry invariant to distance)
// ==========================================

cbuffer TerrainData : register(b0)
{
    float scale;
    float width;
    float depth;
    float base;
}
cbuffer PerFrameData : register(b1)
{
    float4x4 viewproj;
    float4x4 shadowtexmatrices[4];
    float4 eye;
    float4 frustum[6];
}

Texture2D<float4> heightmap : register(t0);
Texture2D<float4> displacementmap : register(t1);

SamplerState hmsampler : register(s0);
SamplerState detailsampler : register(s1);
SamplerState cmpSampler : register(s2);
SamplerState displacementsampler : register(s3);

struct DS_OUTPUT
{
    float4 pos : SV_POSITION;
    float4 shadowpos[4] : TEXCOORD0;
    float3 worldpos : POSITION;
};

struct HS_CONTROL_POINT_OUTPUT
{
    float3 worldpos : POSITION;
};

struct HS_CONSTANT_DATA_OUTPUT
{
    float EdgeTessFactor[4] : SV_TessFactor;
    float InsideTessFactor[2] : SV_InsideTessFactor;
    uint skirt : SKIRT;
};

#define NUM_CONTROL_POINTS 4

// ---- параметры (крутилки) ----
#define PROC_AMP            300.0
#define PROC_TILE           10.0
#define FBM_OCT             6
#define FBM_LAC             2.0
#define FBM_GAIN            0.40
#define WARP_AMP            0.06
#define WARP_FREQ           2.5
#define DISP_AMP            0.35
#define RING_AMP            100.0
#define RING_FREQ           6.0
#define RING_CX             0.5
#define RING_CY             0.5
#define SKIRT_EPS           0.6
#define SKIRT_DROP_SMOOTH0  0.92
#define SKIRT_DROP_SMOOTH1  1.00
#define HEIGHT_BLEND        0.35
#define HEIGHT_POST_POW     0.85

// (ќпционально) дальностной fade/LOD Ч используйте в ѕ» —≈Ћ№Ќќћ шейдере,
// не вли€€ на геометрию в DS.
static const float FADE_NEAR = 48.0;
static const float FADE_FAR = 520.0;

// ---------- utils ----------
float clamp01(float v)
{
    return saturate(v);
}

// ---- тайлимый шум / fbm ----
float hash12(float2 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}
float2 wrap2(float2 i, float2 period)
{
    return float2(fmod(i.x, period.x), fmod(i.y, period.y));
}
float noiseTile(float2 uv, float2 period)
{
    float2 i = floor(uv), f = frac(uv);
    i = wrap2(i, period);
    float a = hash12(wrap2(i + float2(0, 0), period));
    float b = hash12(wrap2(i + float2(1, 0), period));
    float c = hash12(wrap2(i + float2(0, 1), period));
    float d = hash12(wrap2(i + float2(1, 1), period));
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float fbmTile(float2 uv, float2 period)
{
    float a = 0.0, amp = 1.0;
    float2 p = uv, per = period;
    [unroll]
    for (int k = 0; k < FBM_OCT; k++)
    {
        a += noiseTile(p, per) * amp;
        p *= FBM_LAC;
        per *= FBM_LAC;
        amp *= FBM_GAIN;
    }
    return a;
}

// декоративные кольца
float ringHeight(float2 uv)
{
    float2 d = uv - float2(RING_CX, RING_CY);
    float r = length(d);
    float v = 1.0 - abs(sin(r * RING_FREQ * 6.2831853));
    v = pow(saturate(v), 2.0);
    float mask = smoothstep(0.08, 0.25, r) * (1.0 - smoothstep(0.55, 0.85, r));
    return (v * 2.0 - 1.0) * (RING_AMP * 0.5) * mask;
}

// процедурна€ высота
float procHeight(float2 uv)
{
    float2 per = float2(PROC_TILE, PROC_TILE);
    float2 w = (float2(noiseTile(uv * WARP_FREQ, per), noiseTile(uv.yx * (WARP_FREQ * 1.3), per)) * 2.0 - 1.0) * WARP_AMP;
    uv += w;
    float n = fbmTile(uv * PROC_TILE, per);
    float rid = 1.0 - abs(n * 2.0 - 1.0);
    float h = (lerp(n, rid, 0.55) * 2.0 - 1.0) * PROC_AMP;
    return h + ringHeight(uv);
}

float compressSigned(float h)
{
    return sign(h) * pow(abs(h), HEIGHT_POST_POW);
}

// Ѕј«ќ¬џ≈ выборки дл€ геометрии Ч ¬—≈√ƒј LOD=0 (фиксированно)
float heightSampleLOD(float2 uv, float lod)
{
    return heightmap.SampleLevel(hmsampler, uv, lod).x * scale;
}
float3 estimateNormalLOD(float2 uv, float lod)
{
    float dx = 0.35 / width;
    float dy = 0.35 / depth;

    float hL = heightmap.SampleLevel(hmsampler, uv - float2(dx, 0), lod).x * scale;
    float hR = heightmap.SampleLevel(hmsampler, uv + float2(dx, 0), lod).x * scale;
    float hD = heightmap.SampleLevel(hmsampler, uv - float2(0, dy), lod).x * scale;
    float hU = heightmap.SampleLevel(hmsampler, uv + float2(0, dy), lod).x * scale;

    return normalize(float3(hL - hR, hD - hU, 2.0));
}

[domain("quad")]
DS_OUTPUT main(HS_CONSTANT_DATA_OUTPUT input, float2 domain : SV_DomainLocation,
               const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS> patch)
{
    DS_OUTPUT o;

    // базова€ позици€ в патче
    float3 wp = lerp(lerp(patch[0].worldpos, patch[1].worldpos, domain.x),
                     lerp(patch[2].worldpos, patch[3].worldpos, domain.x), domain.y);
    float2 uv = wp.xy / float2(width, depth);

    // --- ¬ј∆Ќќ: дл€ геометрии Ч фиксированный LOD ---
    const float lodGeom = 0.0;

    // высота карты + процедурка Ч Ѕ≈« fade (амплитуда посто€нна)
    float hm = heightSampleLOD(uv, lodGeom);
    float hp = compressSigned(procHeight(uv));
    float zTerrain = lerp(hm, hm + hp, HEIGHT_BLEND);

    // юбка (скат к base)
    float row0 = 0.5 * (patch[0].worldpos.z + patch[1].worldpos.z);
    float row1 = 0.5 * (patch[2].worldpos.z + patch[3].worldpos.z);
    bool row0IsSkirt = abs(row0 - base) < abs(row1 - base);
    float tSkirt = (input.skirt > 0 && input.skirt < 5) ? (row0IsSkirt ? domain.y : (1.0 - domain.y)) : 0.0;
    float zCurtain = lerp(zTerrain, base, tSkirt);

    // нормаль и дисплейс Ч тоже из LOD0, без дальностной амплитуды
    float3 n = estimateNormalLOD(uv, lodGeom);
    float disp = displacementmap.SampleLevel(displacementsampler, uv, lodGeom).a * 2.0 - 1.0;

    float3 p = float3(wp.xy, zCurtain) + n * (disp * DISP_AMP * (1.0 - tSkirt));

    // прижать край юбки
    p.z -= SKIRT_EPS * smoothstep(SKIRT_DROP_SMOOTH0, SKIRT_DROP_SMOOTH1, tSkirt);
    p.z = max(p.z, base);

    // вывод
    float4 w = float4(p, 1);
    o.pos = mul(w, viewproj);
    o.worldpos = p;
    [unroll]
    for (int i = 0; i < 4; i++)
        o.shadowpos[i] = mul(w, shadowtexmatrices[i]);
    return o;
}
