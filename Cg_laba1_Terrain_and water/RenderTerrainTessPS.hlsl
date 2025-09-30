struct LightData
{
    float4 pos;
    float4 amb;
    float4 dif;
    float4 spec;
    float3 att;
    float rng;
    float3 dir;
    float sexp;
};

cbuffer TerrainCB : register(b0)
{
    float scaleVal;
    float worldW;
    float worldD;
    float worldBase;
}

cbuffer FrameCB : register(b1)
{
    float4x4 vpMat;
    float4x4 shadowMats[4];
    float4 eyePos;
    float4 frust[6];
    LightData light;
    bool useTex;
}

Texture2D<float4> hmTex : register(t0);
Texture2D<float4> dispTex : register(t1);
Texture2D<float> shTex : register(t2);
Texture2DArray<float4> detTex : register(t3);

SamplerState hmSamp : register(s0);
SamplerComparisonState shSamp : register(s2);
SamplerState dispSamp : register(s3);

struct PS_IN
{
    float4 pos : SV_POSITION;
    float4 shpos[4] : TEXCOORD0;
    float3 wpos : POSITION;
};

// тени
static const float SMAP_SIZE = 4096.0f;
static const float SMAP_DX = 1.0f / SMAP_SIZE;
static const float4 cols[] =
{
    float4(0.35f, 0.50f, 0.18f, 1.0f),
    float4(0.89f, 0.89f, 0.89f, 1.0f),
    float4(0.31f, 0.25f, 0.20f, 1.0f),
    float4(0.39f, 0.37f, 0.38f, 1.0f)
};

// локальная рамка
float3x3 getFrame(float3 N, float3 p, float2 uv)
{
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 du1 = ddx(uv);
    float2 du2 = ddy(uv);

    float3 dp2p = cross(dp2, N);
    float3 dp1p = cross(N, dp1);
    float3 T = dp2p * du1.x + dp1p * du2.x;
    float3 B = dp2p * du1.y + dp1p * du2.y;

    float invm = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invm, B * invm, N);
}

// возмущение нормали
float3 perturbN(float3 N, float3 V, float2 uv, Texture2D tex, SamplerState smp)
{
    float3 map = 2.0f * tex.Sample(smp, uv).xyz - 1.0f;
    map.z *= 2.0f; // чутка выше
    float3x3 TBN = getFrame(N, -V, uv);
    return normalize(mul(map, TBN));
}

// трипланар выборка
float4 sampleTri(float3 uvw, float3 N, float idx)
{
    float3 an = abs(N) + 1e-5;
    float3 w = pow(an, 8.0);
    w /= (w.x + w.y + w.z);

    float4 tx = detTex.Sample(dispSamp, float3(uvw.yz, idx));
    float4 ty = detTex.Sample(dispSamp, float3(uvw.xz, idx));
    float4 tz = detTex.Sample(dispSamp, float3(uvw.xy, idx));
    return tx * w.x + ty * w.y + tz * w.z;
}

// мягкое смешивание по альфе
float4 blendTex(float4 t1, float b1, float4 t2, float b2)
{
    float depth = 0.2f;
    float ma = max(t1.a + b1, t2.a + b2) - depth;
    float bb1 = max(t1.a + b1 - ma, 0);
    float bb2 = max(t2.a + b2 - ma, 0);
    return (t1 * bb1 + t2 * bb2) / (bb1 + bb2);
}

// по высоте (планар)
float4 getTexHeightPlanar(float height, float3 uvw, float low, float med, float high)
{
    float bounds = scaleVal * 0.005f;
    float transition = scaleVal * 0.6f;
    float lowStart = transition - 2 * bounds;
    float highEnd = transition + 2 * bounds;
    float4 c;

    if (height < lowStart)
    {
        c = detTex.Sample(dispSamp, float3(uvw.xy, low));
    }
    else if (height < transition)
    {
        float4 c1 = detTex.Sample(dispSamp, float3(uvw.xy, low));
        float4 c2 = detTex.Sample(dispSamp, float3(uvw.xy, med));
        float blend = (height - lowStart) / (transition - lowStart);
        c = blendTex(c1, 1 - blend, c2, blend);
    }
    else if (height < highEnd)
    {
        float4 c1 = detTex.Sample(dispSamp, float3(uvw.xy, med));
        float4 c2 = detTex.Sample(dispSamp, float3(uvw.xy, high));
        float blend = (height - transition) / (highEnd - transition);
        c = blendTex(c1, 1 - blend, c2, blend);
    }
    else
    {
        c = detTex.Sample(dispSamp, float3(uvw.xy, high));
    }
    return c;
}

// по высоте (трипланар)
float4 getTexHeightTri(float height, float3 uvw, float3 N, float idx1, float idx2)
{
    float bounds = scaleVal * 0.005f;
    float transition = scaleVal * 0.6f;
    float bStart = transition - bounds;
    float bEnd = transition + bounds;
    float4 c;

    if (height < bStart)
    {
        c = sampleTri(uvw, N, idx1);
    }
    else if (height < bEnd)
    {
        float4 c1 = sampleTri(uvw, N, idx1);
        float4 c2 = sampleTri(uvw, N, idx2);
        float blend = (height - bStart) / (bEnd - bStart);
        c = blendTex(c1, 1 - blend, c2, blend);
    }
    else
    {
        c = sampleTri(uvw, N, idx2);
    }
    return c;
}

// просто цвет по высоте
float4 getColorHeight(float height, float low, float med, float high)
{
    float bounds = scaleVal * 0.005f;
    float transition = scaleVal * 0.6f;
    float lowStart = transition - 2 * bounds;
    float highEnd = transition + 2 * bounds;
    float4 c;

    if (height < lowStart)
    {
        c = cols[low];
    }
    else if (height < transition)
    {
        float4 c1 = cols[low];
        float4 c2 = cols[med];
        float blend = (height - lowStart) / (transition - lowStart);
        c = lerp(c1, c2, blend);
    }
    else if (height < highEnd)
    {
        float4 c1 = cols[med];
        float4 c2 = cols[high];
        float blend = (height - transition) / (highEnd - transition);
        c = lerp(c1, c2, blend);
    }
    else
    {
        c = cols[high];
    }
    return c;
}

// текстуры по уклону
float3 getTexSlope(float slope, float height, float3 N, float3 uvw, float startIdx)
{
    float4 c;
    float blend;
    if (slope < 0.6f)
    {
        blend = slope / 0.6f;
        float4 c1 = getTexHeightPlanar(height, uvw, 0 + startIdx, 3 + startIdx, 1 + startIdx);
        float4 c2 = getTexHeightTri(height, uvw, N, 2 + startIdx, 3 + startIdx);
        c = blendTex(c1, 1 - blend, c2, blend);
    }
    else if (slope < 0.65f)
    {
        blend = (slope - 0.6f) / (0.65f - 0.6f);
        float4 c1 = getTexHeightTri(height, uvw, N, 2 + startIdx, 3 + startIdx);
        float4 c2 = sampleTri(uvw, N, 3 + startIdx);
        c = blendTex(c1, 1 - blend, c2, blend);
    }
    else
    {
        c = sampleTri(uvw, N, 3 + startIdx);
    }
    return c.rgb;
}

// цвета по уклону
float4 getColorSlope(float slope, float height)
{
    float4 c;
    float blend;
    if (slope < 0.6f)
    {
        blend = slope / 0.6f;
        float4 c1 = getColorHeight(height, 0, 3, 1);
        float4 c2 = getColorHeight(height, 2, 3, 3);
        c = lerp(c1, c2, blend);
    }
    else if (slope < 0.65f)
    {
        blend = (slope - 0.6f) / (0.65f - 0.6f);
        float4 c1 = getColorHeight(height, 2, 3, 3);
        float4 c2 = cols[3];
        c = lerp(c1, c2, blend);
    }
    else
    {
        c = cols[3];
    }
    return c;
}

// нормаль, зависящая от высоты/уклона
float3 perturbNHeightSlope(float height, float slope, float3 N, float3 V, float3 uvw)
{
    float3 c = getTexSlope(slope, height, N, uvw, 0) - 0.5f;
    float3x3 TBN = getFrame(N, -V, uvw);
    return normalize(mul(c, TBN));
}

// текстуры по дистанции
float4 distTexturing(float height, float slope, float3 N, float3 V, float3 uvw)
{
    float dist = length(V);

    if (dist > 75)
        return getColorSlope(slope, height);
    else if (dist > 25)
    {
        float blend = (dist - 25.0f) / (75.0f - 25.0f);
        float4 c1 = float4(getTexSlope(slope, height, N, uvw, 4), 1);
        float4 c2 = getColorSlope(slope, height);
        return lerp(c1, c2, blend);
    }
    else
        return float4(getTexSlope(slope, height, N, uvw, 4), 1);
}

// нормали по дистанции
float3 distNormal(float height, float slope, float3 N, float3 V, float3 uvw)
{
    float dist = length(V);

    float3 N1 = perturbN(N, V, uvw / 16, dispTex, dispSamp);
	
    if (dist > 150)
        return N;

    if (dist > 100)
    {
        float blend = (dist - 100.0f) / 50.0f;
        return lerp(N1, N, blend);
    }

    float3 N2 = perturbNHeightSlope(height, slope, N1, V, uvw);

    if (dist > 50)
        return N1;

    if (dist > 25)
    {
        float blend = (dist - 25.0f) / 25.0f;
        return lerp(N2, N1, blend);
    }

    return N2;
}

// нормаль из карты высот (собель)
float3 estimateN(float2 tc)
{
    float2 b = tc + float2(0.0f, -0.3f / worldD);
    float2 c = tc + float2(0.3f / worldW, -0.3f / worldD);
    float2 d = tc + float2(0.3f / worldW, 0.0f);
    float2 e = tc + float2(0.3f / worldW, 0.3f / worldD);
    float2 f = tc + float2(0.0f, 0.3f / worldD);
    float2 g = tc + float2(-0.3f / worldW, 0.3f / worldD);
    float2 h = tc + float2(-0.3f / worldW, 0.0f);
    float2 i = tc + float2(-0.3f / worldW, -0.3f / worldD);

    float zb = hmTex.SampleLevel(hmSamp, b, 0).x * scaleVal;
    float zc = hmTex.SampleLevel(hmSamp, c, 0).x * scaleVal;
    float zd = hmTex.SampleLevel(hmSamp, d, 0).x * scaleVal;
    float ze = hmTex.SampleLevel(hmSamp, e, 0).x * scaleVal;
    float zf = hmTex.SampleLevel(hmSamp, f, 0).x * scaleVal;
    float zg = hmTex.SampleLevel(hmSamp, g, 0).x * scaleVal;
    float zh = hmTex.SampleLevel(hmSamp, h, 0).x * scaleVal;
    float zi = hmTex.SampleLevel(hmSamp, i, 0).x * scaleVal;

    float x = zg + 2 * zh + zi - zc - 2 * zd - ze;
    float y = 2 * zb + zc + zi - ze - 2 * zf - zg;
    float z = 8.0f;

    return normalize(float3(x, y, z));
}

// pcf 3x3
float calcShadow(float4 shPosH)
{
    float depth = shPosH.z;
    const float dx = SMAP_DX;

    float percent = 0.0f;
    const float2 offs[9] =
    {
        float2(-dx, -dx), float2(0, -dx), float2(dx, -dx),
        float2(-dx, 0), float2(0, 0), float2(dx, 0),
        float2(-dx, dx), float2(0, dx), float2(dx, dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
        percent += shTex.SampleCmpLevelZero(shSamp, shPosH.xy + offs[i], depth);

    return percent / 9.0f;
}

// выбор каскада
float pickCascade(float4 shpos[4])
{
    if (max(abs(shpos[0].x - 0.25), abs(shpos[0].y - 0.25)) < 0.247)
        return calcShadow(shpos[0]);
    if (max(abs(shpos[1].x - 0.25), abs(shpos[1].y - 0.75)) < 0.247)
        return calcShadow(shpos[1]);
    if (max(abs(shpos[2].x - 0.75), abs(shpos[2].y - 0.25)) < 0.247)
        return calcShadow(shpos[2]);
    return calcShadow(shpos[3]);
}

// основной пиксельный
float4 main(PS_IN I) : SV_TARGET
{
    float3 Nhm = estimateN(I.wpos / worldW);
    float3 Vdir = eyePos.xyz - I.wpos;

    float slope = acos(Nhm.z);
    float3 N = distNormal(I.wpos.z, slope, Nhm, Vdir, I.wpos / 2);

    float4 col;
    if (useTex)
        col = distTexturing(I.wpos.z, slope, N, Vdir, I.wpos / 2);
    else
        col = getColorSlope(slope, I.wpos.z);

    float shadowFactor = 1.0f; // если надо — заменить на pickCascade(I.shpos)

    float ndl = dot(-light.dir, N);
    float4 diff = max(shadowFactor, light.amb) * light.dif * ndl;

    float3 R = reflect(light.dir, N);
    float3 toEye = normalize(eyePos.xyz - I.wpos);
    float specPow = pow(max(dot(R, toEye), 0.0f), 2.0f);
    float4 spec = shadowFactor * 0.1f * light.spec * specPow;

    return (diff + spec) * col;
}