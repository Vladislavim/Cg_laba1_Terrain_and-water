cbuffer TerrainData : register(b0)
{
    float scale;
    float width;
    float depth;
    float base;
}
cbuffer ShadowConstants : register(b1)
{
    float4x4 shadowmatrix;
}
Texture2D<float4> heightmap : register(t0);
Texture2D<float4> displacementmap : register(t1);
SamplerState hmsampler : register(s0);
SamplerState displacementsampler : register(s1);

struct DS_OUTPUT
{
    float4 pos : SV_POSITION;
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

// те же параметры, чтобы тени не гуляли
#define PROC_AMP 120.0
#define PROC_TILE 6.0
#define FBM_OCT 3
#define FBM_LAC 2.0
#define FBM_GAIN 0.40
#define WARP_AMP 0.06
#define WARP_FREQ 2.5
#define DISP_AMP 0.15
#define RING_AMP 60.0
#define RING_FREQ 6.0
#define RING_CX 0.5
#define RING_CY 0.5
#define SKIRT_EPS 0.6
#define SKIRT_DROP_SMOOTH0 0.92
#define SKIRT_DROP_SMOOTH1 1.00
#define HEIGHT_BLEND 0.35
#define HEIGHT_POST_POW 0.85

// шум/функции те же
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
float ringHeight(float2 uv)
{
    float2 d = uv - float2(RING_CX, RING_CY);
    float r = length(d);
    float v = 1.0 - abs(sin(r * RING_FREQ * 6.2831853));
    v = pow(saturate(v), 2.0);
    float mask = smoothstep(0.08, 0.25, r) * (1.0 - smoothstep(0.55, 0.85, r));
    return (v * 2.0 - 1.0) * (RING_AMP * 0.5) * mask;
}
float compressSigned(float h)
{
    return sign(h) * pow(abs(h), HEIGHT_POST_POW);
}

// высота для теней (та же логика)
float procHeight(float2 uv)
{
    float2 per = float2(PROC_TILE, PROC_TILE);
    float2 w = (float2(noiseTile(uv * WARP_FREQ, per), noiseTile(uv.yx * (WARP_FREQ * 1.3), per)) * 2.0 - 1.0) * WARP_AMP;
    uv += w;
    float n = fbmTile(uv * PROC_TILE, per);
    float rid = 1.0 - abs(n * 2.0 - 1.0);
    return (lerp(n, rid, 0.55) * 2.0 - 1.0) * PROC_AMP + ringHeight(uv);
}
float heightSample(float2 uv)
{
    float hm = heightmap.SampleLevel(hmsampler, uv, 0).x * scale;
    float hp = compressSigned(procHeight(uv));
    return lerp(hm, hm + hp, HEIGHT_BLEND);
}

[domain("quad")]
DS_OUTPUT main(HS_CONSTANT_DATA_OUTPUT input, float2 domain : SV_DomainLocation,
               const OutputPatch<HS_CONTROL_POINT_OUTPUT, NUM_CONTROL_POINTS> patch)
{
    DS_OUTPUT o;

    // позиция в патче
    float3 wp = lerp(lerp(patch[0].worldpos, patch[1].worldpos, domain.x),
                     lerp(patch[2].worldpos, patch[3].worldpos, domain.x), domain.y);
    float2 uv = wp.xy / float2(width, depth);

    // высота
    float zTerrain = heightSample(uv);

    // юбка как в основном DS
    float row0 = .5 * (patch[0].worldpos.z + patch[1].worldpos.z);
    float row1 = .5 * (patch[2].worldpos.z + patch[3].worldpos.z);
    bool row0IsSkirt = abs(row0 - base) < abs(row1 - base);
    float tSkirt = (input.skirt > 0 && input.skirt < 5) ? (row0IsSkirt ? domain.y : (1.0 - domain.y)) : 0.0;
    float zCurtain = lerp(zTerrain, base, tSkirt);

    // чуть сдвинем по «псевдо-нормали», ослабим на юбке
    float d = displacementmap.SampleLevel(displacementsampler, uv, 0).a * 2.0 - 1.0;
    float dx = 0.35 / width, dy = 0.35 / depth;
    float hL = heightSample(uv - float2(dx, 0)), hR = heightSample(uv + float2(dx, 0));
    float hD = heightSample(uv - float2(0, dy)), hU = heightSample(uv + float2(0, dy));
    float3 n = normalize(float3(hL - hR, hD - hU, 2.0));
    float3 p = float3(wp.xy, zCurtain) + n * (d * DISP_AMP * (1.0 - tSkirt));

    // микро-прижим и ограничение
    p.z -= SKIRT_EPS * smoothstep(SKIRT_DROP_SMOOTH0, SKIRT_DROP_SMOOTH1, tSkirt);
    p.z = max(p.z, base);

    o.pos = mul(float4(p, 1), shadowmatrix);
    return o;
}
