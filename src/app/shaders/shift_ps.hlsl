// Copia esatta pixel per pixel del desktop catturato, spostata di un offset intero.
// I bordi scoperti ripetono il pixel di bordo (clamp), niente nero e niente filtri.
// Oscuramento: il colore viene moltiplicato per (1 - livello) in luce lineare; il
// livello si legge con interpolazione bilineare dalla griglia di celle 16x16 px.
// Con livello 0 il pixel esce identico bit per bit.
Texture2D<float4> desktop : register(t0);
Texture2D<float> levels : register(t1);
SamplerState linear_clamp : register(s0);

cbuffer ShiftConstants : register(b0) {
  int2 offset;
  uint2 size_minus_one;
  float2 grid_size;  // colonne*16, righe*16 in pixel
  float sdr_white;   // valore del bianco nel formato di uscita
  uint flags;        // 1 SDR, 2 evidenzia, 4 oscuramento attivo (vedi shift_constants.h)
};

static const uint kFlagSdr = 1;
static const uint kFlagHighlight = 2;
static const uint kFlagDim = 4;
static const float kMaxLevel = 0.8;
// #ffb020 in luce lineare.
static const float3 kAmber = float3(1.0, 0.4342, 0.0144);

float3 SrgbToLinear(float3 c) {
  return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float3 LinearToSrgb(float3 c) {
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
}

float4 main(float4 pos : SV_Position) : SV_Target {
  const int2 src = clamp(int2(pos.xy) - offset, int2(0, 0), int2(size_minus_one));
  const float4 c = desktop.Load(int3(src, 0));
  if ((flags & kFlagDim) == 0) return c;
  const float level = levels.SampleLevel(linear_clamp, (float2(src) + 0.5) / grid_size, 0);
  if (level <= 0.0) return c;

  const bool sdr = (flags & kFlagSdr) != 0;
  float3 rgb = sdr ? SrgbToLinear(c.rgb) : c.rgb;
  rgb *= 1.0 - level;
  if (flags & kFlagHighlight) rgb = lerp(rgb, kAmber * sdr_white, saturate(level / kMaxLevel) * 0.7);
  if (sdr) rgb = saturate(LinearToSrgb(rgb));
  return float4(rgb, c.a);
}
