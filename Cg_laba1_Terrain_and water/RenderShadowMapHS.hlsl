cbuffer ShadowConstants : register(b1)
{
    float4x4 shadowmatrix;
    float4 eye;
    float4 frustum[4];
}

struct VS_OUTPUT
{
    float3 worldpos : POSITION0;
    float3 aabbmin : POSITION1;
    float3 aabbmax : POSITION2;
    uint skirt : SKIRT;
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

bool aabbBehindPlane(float3 c, float3 e, float4 pl)
{
    float3 n = abs(pl.xyz);
    float s = dot(float4(c, 1), pl);
    float r = dot(e, n);
    return (s + r) < 0.0f;
}
bool aabbOutside(float3 c, float3 e, float4 planes[4])
{
    [unroll]
    for (int i = 0; i < 4; i++)
        if (aabbBehindPlane(c, e, planes[i]))
            return true;
    return false;
}

float CalcTessFactor(float3 p)
{
    float d = distance(p, eye.xyz);
    float s = saturate((d - 128.0f) / (256.0f - 128.0f));
    return pow(2.0f, lerp(4.0f, 0.0f, s));
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

    float t = CalcTessFactor(c);
    [unroll]
    for (int i = 0; i < 4; i++)
        o.EdgeTessFactor[i] = t;
    o.InsideTessFactor[0] = t;
    o.InsideTessFactor[1] = t;
    return o;
}

[domain("quad")]
[partitioning("integer")]
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
