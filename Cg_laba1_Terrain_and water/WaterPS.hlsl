struct PSIn
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

float4 main(PSIn i) : SV_Target
{
    // простая вода: синий c небольшой прозрачностью
    float3 deep = float3(0.02, 0.12, 0.25);
    float3 shallow = float3(0.05, 0.35, 0.6);
    float lerpK = 0.55; // можно варьировать по высоте/нормали
    float3 col = lerp(deep, shallow, lerpK);
    return float4(col, 0.5); // альфа 0.5
}
