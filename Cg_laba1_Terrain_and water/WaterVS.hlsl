cbuffer FrameCB : register(b0)
{
    float4x4 gViewProj;
};

cbuffer WaterCB : register(b1)
{
    float2 gCenter; // центр плоскости по X,Y
    float gHalfSize; // половина размера (радиус)
    float gLevel; // уровень воды (ось Z)
    float gTime; // врем€
    float gGrid; // размер сетки (N)
    float gWaveAmp; // амплитуда базовых волн
    float gWaveLen = 10.0f; // длина волны (масштаб)
    float gWaveSpeed; // скорость
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

static const float PI = 3.14159265f;

float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float hash(float2 p)
{
    float h = dot(p, float2(127.1f, 311.7f));
    return frac(sin(h) * 43758.5453f);
}

float noise2(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float a = hash(i + float2(0, 0));
    float b = hash(i + float2(1, 0));
    float c = hash(i + float2(0, 1));
    float d = hash(i + float2(1, 1));
    float2 u = float2(fade(f.x), fade(f.y));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y); // [0..1]
}

// м€гка€ р€бь; возвращаем [-1..1]
float fbm(float2 p)
{
    float a = 0.0, amp = 0.5;
    [unroll]
    for (int k = 0; k < 3; k++)
    {
        a += noise2(p) * amp;
        p = p * 2.0 + 17.17;
        amp *= 0.5;
    }
    return a * 2.0 - 1.0;
}

float3 Gerstner(float2 xy, float2 dir, float amp, float lambda, float speed, float time)
{
    dir = normalize(dir);
    float k = 2.0f * PI / max(lambda, 0.001f);
    float w = sqrt(9.81f * k) * max(speed, 0.1f);
    float ph = dot(dir, xy) * k + w * time;
    float s, c;
    sincos(ph, s, c);

    float2 dispXY = dir * (amp * 0.05f * c);
    float dispZ = amp * s;
    return float3(dispXY, dispZ);
}

float2 cellCornerUV(uint v)
{
    static const float2 lut[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1)
    };
    return lut[v];
}

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;

    // адресаци€ вершин
    uint N = (uint) max(gGrid, 1.0f);
    uint tri = vid / 6u;
    uint v = vid % 6u;
    uint i = tri % N;
    uint j = tri / N;

    float2 uvCell = (float2(i, j) + cellCornerUV(v)) / max(gGrid, 1.0f);
    float2 xy = (uvCell * 2.0f - 1.0f) * gHalfSize + gCenter;

    // базовые волны: больша€ + средн€€ + мелка€
    float A0 = gWaveAmp;
    float L0 = gWaveLen;
    float S0 = gWaveSpeed;
    float A1 = gWaveAmp * 0.6f;
    float L1 = gWaveLen * 0.6f;
    float S1 = gWaveSpeed * 1.2f;
    float A2 = gWaveAmp * 0.35f;
    float L2 = gWaveLen * 0.35f;
    float S2 = gWaveSpeed * 1.5f;

    float2 D0 = float2(1.0, 0.2);
    float2 D1 = float2(0.3, 1.0);
    float2 D2 = float2(-0.8, 0.6);

    float3 w0 = Gerstner(xy, D0, A0, L0, S0, gTime);
    float3 w1 = Gerstner(xy, D1, A1, L1, S1, gTime);
    float3 w2 = Gerstner(xy, D2, A2, L2, S2, gTime);

    float2 xyDisp = w0.xy + w1.xy + w2.xy; // лЄгкое, стабильное смещение
    float zBase = w0.z + w1.z + w2.z;

    // ѕерлин только в Z: м€гка€ р€бь без Ђколбасыї
    float safeLen = max(gWaveLen, 0.001f);
    float2 pNoise = (xy / safeLen) * 1.8f; // масштаб шума
    float tNoise = gTime * (gWaveSpeed * 1.3f); // медленно
    float zNoise = fbm(pNoise + tNoise) * (gWaveAmp * 0.25f);

    float2 xyFinal = xy + xyDisp;
    float zFinal = gLevel + zBase + zNoise; // Z = герстнер + м€гкий ѕерлин

    float4 world = float4(xyFinal, zFinal, 1.0f);
    o.worldPos = world.xyz;
    o.pos = mul(world, gViewProj);
    return o;
}
