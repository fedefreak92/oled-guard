// Oscuramento delle aree statiche: passaggi di calcolo sulla griglia di celle 16x16 px.
// Il comportamento e' quello di src/core/dim_model.cpp (oracolo dei test GPU).

cbuffer DimConstants : register(b0) {
  float now_s;
  float dt_s;
  float strength;
  float delay_s;
  float ramp_s;
  float recover_s;
  float large_delay_s;
  float large_strength;
  float bright_threshold;
  float large_fraction;
  float sdr_white;  // valore del bianco nel formato catturato (1 in SDR)
  uint flags;       // vedi kFlag*
  uint cols;
  uint rows;
  uint width;
  uint height;
};

static const uint kFlagSdr = 1;
static const uint kFlagSuspended = 2;
static const uint kFlagEnabled = 4;
static const uint kFlagLargeEnabled = 8;
static const uint kFlagFirstFrame = 16;

static const int kCellPx = 16;
static const int kRadius = 16;
static const float kBrightPixelFraction = 0.125;
static const float kDetailRange = 0.10;
static const float kMaxLevel = 0.8;
static const float kCommandRecoverS = 1.0;
static const float kLargeRecoverS = 30.0;

// ------------------------------------------------------------------ rilevamento

Texture2D<float4> frame_tex : register(t0);
Texture2D<float4> previous_tex : register(t1);
RWTexture2D<float> last_change_out : register(u0);
RWTexture2D<uint> flags_out : register(u1);

groupshared uint g_changed;
groupshared uint g_bright;
groupshared uint g_min;
groupshared uint g_max;

float SrgbToLinear(float c) {
  return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float Luma(float4 c) {
  float3 rgb = c.rgb;
  if (flags & kFlagSdr) rgb = float3(SrgbToLinear(rgb.r), SrgbToLinear(rgb.g), SrgbToLinear(rgb.b));
  return max(0.0, dot(rgb, float3(0.2126, 0.7152, 0.0722)) / sdr_white);
}

[numthreads(16, 16, 1)]
void CSDetect(uint3 cell : SV_GroupID, uint3 tid : SV_GroupThreadID, uint index : SV_GroupIndex) {
  if (index == 0) {
    g_changed = 0;
    g_bright = 0;
    g_min = 0x7f7fffff;  // FLT_MAX: con valori >= 0 l'ordine dei bit e' quello dei float
    g_max = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  const uint2 pixel = cell.xy * (uint)kCellPx + tid.xy;
  if (pixel.x < width && pixel.y < height) {
    const float4 now = frame_tex.Load(int3(pixel, 0));
    const float4 before = previous_tex.Load(int3(pixel, 0));
    if (any(now != before)) InterlockedOr(g_changed, 1u);
    const float l = Luma(now);
    if (l >= bright_threshold) InterlockedAdd(g_bright, 1u);
    InterlockedMin(g_min, asuint(l));
    InterlockedMax(g_max, asuint(l));
  }
  GroupMemoryBarrierWithGroupSync();

  if (index == 0) {
    const uint w = min(width - cell.x * (uint)kCellPx, (uint)kCellPx);
    const uint h = min(height - cell.y * (uint)kCellPx, (uint)kCellPx);
    const bool bright = (float)g_bright >= kBrightPixelFraction * (float)(w * h);
    const bool content = bright || asfloat(g_max) - asfloat(g_min) >= kDetailRange;
    if (g_changed != 0 || (flags & kFlagFirstFrame)) last_change_out[cell.xy] = now_s;
    flags_out[cell.xy] = (bright ? 1u : 0u) | (content ? 2u : 0u);
  }
}

// ------------------------------------------------------------------ densita' e grandi aree

Texture2D<uint> flags_in : register(t0);
Texture2D<float> last_change_in : register(t1);
Texture2D<float2> sums_in : register(t0);
Texture2D<float> grid_in : register(t0);
Texture2D<float> large_in : register(t2);
RWTexture2D<float2> sums_out : register(u0);
RWTexture2D<float> grid_out : register(u0);
RWTexture2D<float> levels_io : register(u0);
// Bit 0-1: ultima classe (0 nessuna, 1 elemento piccolo, 2 grande area);
// bit 2: ritorno lento in corso (classe cambiata con il livello sopra il bersaglio).
RWTexture2D<uint> class_io : register(u1);

bool ContentStatic(int2 c) {
  return (flags_in[c] & 2u) != 0 && now_s - last_change_in[c] >= delay_s;
}

[numthreads(8, 8, 1)]
void CSDensityH(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int x0 = max(0, (int)id.x - kRadius);
  const int x1 = min((int)cols - 1, (int)id.x + kRadius);
  float sum = 0;
  for (int x = x0; x <= x1; ++x) sum += ContentStatic(int2(x, id.y)) ? 1.0 : 0.0;
  sums_out[id.xy] = float2(sum, x1 - x0 + 1);
}

[numthreads(8, 8, 1)]
void CSDensityV(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int y0 = max(0, (int)id.y - kRadius);
  const int y1 = min((int)rows - 1, (int)id.y + kRadius);
  float2 s = 0;
  for (int y = y0; y <= y1; ++y) s += sums_in[int2(id.x, y)];
  grid_out[id.xy] = s.x / s.y;
}

[numthreads(8, 8, 1)]
void CSMaxH(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int x0 = max(0, (int)id.x - kRadius);
  const int x1 = min((int)cols - 1, (int)id.x + kRadius);
  float m = 0;
  for (int x = x0; x <= x1; ++x) m = max(m, grid_in[int2(x, id.y)]);
  grid_out[id.xy] = m;
}

[numthreads(8, 8, 1)]
void CSMaxV(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int y0 = max(0, (int)id.y - kRadius);
  const int y1 = min((int)rows - 1, (int)id.y + kRadius);
  float m = 0;
  for (int y = y0; y <= y1; ++y) m = max(m, grid_in[int2(id.x, y)]);
  grid_out[id.xy] = m;
}

// ------------------------------------------------------------------ livello

[numthreads(8, 8, 1)]
void CSLevel(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int2 c = int2(id.xy);
  const float age = now_s - last_change_in[c];
  const bool bright = (flags_in[c] & 1u) != 0;
  const bool m = bright && age >= delay_s;
  const bool large = large_in[c] >= large_fraction;
  const bool enabled = (flags & kFlagEnabled) != 0;
  const bool suspended = (flags & kFlagSuspended) != 0;

  float cap = 0;
  if (enabled && !suspended && m) {
    if (!large) cap = strength;
    else if (flags & kFlagLargeEnabled) cap = large_strength;
  }
  const float start = large ? large_delay_s : delay_s;
  const float target = cap * saturate((age - start) / ramp_s);

  const uint cls = !m ? 0u : (large ? 2u : 1u);
  const uint previous = class_io[c];
  float level = levels_io[c];
  bool slow = (previous & 4u) != 0;
  if (cls != (previous & 3u) && level > target) slow = true;
  if (level < target) {
    level = min(target, level + cap / ramp_s * dt_s);
  } else if (level > target) {
    float rate = strength / kLargeRecoverS;  // da strength a 0 in 30 s
    if (age < delay_s || !bright) rate = max(rate, kMaxLevel / recover_s);
    if (suspended || !enabled || (cls != 0 && !slow && level > cap)) rate = max(rate, kMaxLevel / kCommandRecoverS);
    level = max(target, level - rate * dt_s);
  }
  if (level <= target) slow = false;
  class_io[c] = cls | (slow ? 4u : 0u);
  levels_io[c] = level;
}
