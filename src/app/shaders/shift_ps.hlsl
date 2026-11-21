// Copia esatta pixel per pixel del desktop catturato, spostata di un offset intero.
// I bordi scoperti ripetono il pixel di bordo (clamp), niente nero e niente filtri.
// Oscuramento: il colore viene moltiplicato per (1 - livello) in luce lineare; il
// livello si legge con interpolazione bilineare dalla griglia di celle 16x16 px.
// Con livello 0 il pixel esce identico bit per bit.
// Luminosita' e bordi scuriti (flag 8): un fattore in luce lineare che dipende dalla
// posizione sullo schermo, stessa formula di src/core/shade_model.cpp.
Texture2D<float4> desktop : register(t0);
Texture2D<float> levels : register(t1);
SamplerState linear_clamp : register(s0);

cbuffer ShiftConstants : register(b0) {
  int2 offset;
  uint2 size_minus_one;
  float2 grid_size;  // colonne*16, righe*16 in pixel
  float sdr_white;   // valore del bianco nel formato di uscita
  uint flags;        // 1 SDR, 2 evidenzia, 4 oscuramento attivo, 8 luminosita'/bordi (vedi shift_constants.h)
  float4 spread;     // bordi: sinistra, destra, alto, basso
  float brightness;
  float vignette_strength;
  float2 pad;
};

static const uint kFlagSdr = 1;
static const uint kFlagHighlight = 2;
static const uint kFlagDim = 4;
static const uint kFlagShade = 8;
static const float kMaxLevel = 0.8;
// #ffb020 in luce lineare.
static const float3 kAmber = float3(1.0, 0.4342, 0.0144);

float3 SrgbToLinear(float3 c) {
  return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float3 LinearToSrgb(float3 c) {
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
}

// 1 sul bordo, 0 da `s` in poi (smoothstep); s = 0: lato spento.
float Edge(float d, float s) {
  if (s <= 0.0) return 0.0;
  const float t = saturate(d / s);
  return 1.0 - t * t * (3.0 - 2.0 * t);
}

float ShadeFactor(float2 uv) {
  const float v = max(max(Edge(uv.x, spread.x), Edge(1.0 - uv.x, spread.y)),
                      max(Edge(uv.y, spread.z), Edge(1.0 - uv.y, spread.w)));
  return brightness * (1.0 - vignette_strength * v);
}

float4 main(float4 pos : SV_Position) : SV_Target {
  const int2 src = clamp(int2(pos.xy) - offset, int2(0, 0), int2(size_minus_one));
  const float4 c = desktop.Load(int3(src, 0));
  if ((flags & (kFlagDim | kFlagShade)) == 0) return c;
  const float level = (flags & kFlagDim) ? levels.SampleLevel(linear_clamp, (float2(src) + 0.5) / grid_size, 0) : 0.0;
  // I bordi seguono lo schermo, non l'immagine spostata.
  const float shade = (flags & kFlagShade) ? ShadeFactor(pos.xy / float2(size_minus_one + 1)) : 1.0;
  if (level <= 0.0 && shade == 1.0) return c;

  const bool sdr = (flags & kFlagSdr) != 0;
  float3 rgb = sdr ? SrgbToLinear(c.rgb) : c.rgb;
  rgb *= (1.0 - level) * shade;
  if (flags & kFlagHighlight) rgb = lerp(rgb, kAmber * sdr_white, saturate(level / kMaxLevel) * 0.7);
  if (sdr) rgb = saturate(LinearToSrgb(rgb));
  return float4(rgb, c.a);
}
