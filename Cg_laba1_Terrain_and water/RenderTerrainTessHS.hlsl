cbuffer PerFrameData : register(b1)
{
    float4x4 viewproj;
    float4x4 shadowtexmatrices[4];
    float4 eye;
    float4 frustum[6];
}

struct VS_OUTPUT
{
    float3 worldpos : POSITION0;
    float3 aabbmin : POSITION1;
    float3 aabbmax : POSITION2;
    uint skirt : SKIRT; // 0..4 дл€ юбок, 5 Ч обычное тело
};

struct HS_CONTROL_POINT_OUTPUT
{
    float3 worldpos : POSITION;
};

struct HS_CONSTANT_DATA_OUTPUT
{
    float EdgeTessFactor[4] : SV_TessFactor; // top, bottom, left, right
    float InsideTessFactor[2] : SV_InsideTessFactor;
    uint skirt : SKIRT;
};

#define NUM_CONTROL_POINTS 4

bool aabbBehindPlane(float3 c, float3 e, float4 pl)
{
    float3 n = abs(pl.xyz);
    float s = dot(float4(c, 1), pl);
    float r = dot(e, n);
    return (s + r) < 0.0f;
}
bool aabbOutside(float3 c, float3 e, float4 planes[6])
{
    [unroll]
    for (int i = 0; i < 6; i++)
        if (aabbBehindPlane(c, e, planes[i]))
            return true;
    return false;
}

// ---- LOD controls ----
static const float LOD_NEAR = 32.0;
static const float LOD_FAR = 400.0;
static const float HARD_CUTOFF = 2000.0;

float CalcTessFactor(float3 p)
{
    float d = distance(p, eye.xyz);
    if (d > HARD_CUTOFF)
        return 0.0;

    float s = saturate((d - LOD_NEAR) / (LOD_FAR - LOD_NEAR));
    s = pow(s, 1.5);

    // 64..2
    float t = pow(2.0, lerp(6.0, 1.0, s));
    return clamp(t, 1.0f, 64.0f);
}

HS_CONSTANT_DATA_OUTPUT CalcHSPatchConstants(
    InputPatch<VS_OUTPUT, NUM_CONTROL_POINTS> ip,
    uint PatchID : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT o;
    o.skirt = ip[0].skirt;

    float3 vMin = ip[0].aabbmin;
    float3 vMax = ip[0].aabbmax;
    float3 c = 0.5f * (vMin + vMax);
    float3 e = 0.5f * (vMax - vMin);

    if (aabbOutside(c, e, frustum))
    {
        [unroll]
        for (int i = 0; i < 4; i++)
            o.EdgeTessFactor[i] = 0.0f;
        o.InsideTessFactor[0] = 0.0f;
        o.InsideTessFactor[1] = 0.0f;
        return o;
    }

    if (o.skirt == 0)
    {
        [unroll]
        for (int i = 0; i < 4; i++)
            o.EdgeTessFactor[i] = 1.0f;
        o.InsideTessFactor[0] = 1.0f;
        o.InsideTessFactor[1] = 1.0f;
        return o;
    }

    float3 edgeMid[4] =
    {
        0.5 * (ip[0].worldpos + ip[1].worldpos), // top
        0.5 * (ip[2].worldpos + ip[3].worldpos), // bottom
        0.5 * (ip[0].worldpos + ip[2].worldpos), // left
        0.5 * (ip[1].worldpos + ip[3].worldpos) // right
    };

    [unroll]
    for (int i = 0; i < 4; i++)
        o.EdgeTessFactor[i] = CalcTessFactor(edgeMid[i]);

    o.InsideTessFactor[0] = 0.5 * (o.EdgeTessFactor[0] + o.EdgeTessFactor[1]);
    o.InsideTessFactor[1] = 0.5 * (o.EdgeTessFactor[2] + o.EdgeTessFactor[3]);
    return o;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(NUM_CONTROL_POINTS)]
[patchconstantfunc("CalcHSPatchConstants")]
HS_CONTROL_POINT_OUTPUT main(
    InputPatch<VS_OUTPUT, NUM_CONTROL_POINTS> ip,
    uint id : SV_OutputControlPointID,
    uint pid : SV_PrimitiveID)
{
    HS_CONTROL_POINT_OUTPUT o;
    o.worldpos = ip[id].worldpos;
    return o;
}