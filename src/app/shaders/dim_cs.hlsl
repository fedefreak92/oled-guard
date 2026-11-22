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
  float motion_fraction;
  float sdr_white;  // valore del bianco nel formato catturato (1 in SDR)
  uint flags;       // vedi kFlag*
  uint cols;
  uint rows;
  uint width;
  uint height;
  uint active0;  // zone delle finestre attive (0 = nessuna)
  uint active1;
  uint2 pad;
  // Riassunto dei segnali: luminosita' e bordi (come ShadeParams, src/core/shade_model.h).
  float4 shade_spread;  // sinistra, destra, alto, basso
  float shade_brightness;
  float shade_strength;
  uint2 pad2;
};

static const uint kFlagSdr = 1;
static const uint kFlagSuspended = 2;
static const uint kFlagEnabled = 4;
static const uint kFlagLargeEnabled = 8;
static const uint kFlagFirstFrame = 16;
static const uint kFlagRestore = 32;
static const uint kFlagWindowsLikeDesktop = 64;

static const int kCellPx = 16;
static const int kRadius = 16;
static const float kBrightPixelFraction = 0.125;
static const float kMotionWindowS = 10.0;
static const float kBlipS = 15.0;  // deviazione breve: la cella riprende l'eta' se torna uguale
static const float kMaxLevel = 0.8;
static const float kCommandRecoverS = 1.0;
static const float kLargeRecoverS = 30.0;
static const uint kZoneShell = 0;  // altrimenti numero della finestra dell'app

// ------------------------------------------------------------------ rilevamento

Texture2D<float4> frame_tex : register(t0);
Texture2D<float4> previous_tex : register(t1);
Texture2D<uint> saved_hash : register(t2);
Texture2D<float> saved_age : register(t3);
RWTexture2D<float> last_change_out : register(u0);
RWTexture2D<uint> flags_out : register(u1);
RWTexture2D<uint> hash_out : register(u2);
// Contenuto di riferimento, da quando c'e', inizio della deviazione in corso (-1 nessuna).
RWTexture2D<uint> anchor_hash_io : register(u3);
RWTexture2D<float> anchor_time_io : register(u4);
RWTexture2D<float> excursion_io : register(u5);
// Statistiche: luminanza media della cella e istante dell'ultimo cambio reale dei pixel
// (le finestre attive non lo toccano, a differenza di last_change).
RWTexture2D<float> luma_out : register(u6);
RWTexture2D<float> content_change_out : register(u7);

groupshared uint g_changed;
groupshared uint g_bright;
groupshared uint g_hash_add;
groupshared uint g_hash_xor;
groupshared uint g_luma;  // somma della luminanza x kLumaFixed, saturata a kLumaMax per pixel

static const float kLumaMax = 64.0;
static const float kLumaFixed = 1024.0;

float SrgbToLinear(float c) {
  return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4);
}

float Luma(float4 c) {
  float3 rgb = c.rgb;
  if (flags & kFlagSdr) rgb = float3(SrgbToLinear(rgb.r), SrgbToLinear(rgb.g), SrgbToLinear(rgb.b));
  return max(0.0, dot(rgb, float3(0.2126, 0.7152, 0.0722)) / sdr_white);
}

// Mescolamento PCG: firma dei pixel di una cella, indipendente dall'ordine dei thread.
uint Pcg(uint v) {
  const uint state = v * 747796405u + 2891336453u;
  const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

[numthreads(16, 16, 1)]
void CSDetect(uint3 cell : SV_GroupID, uint3 tid : SV_GroupThreadID, uint index : SV_GroupIndex) {
  if (index == 0) {
    g_changed = 0;
    g_bright = 0;
    g_hash_add = 0;
    g_hash_xor = 0;
    g_luma = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  const uint2 pixel = cell.xy * (uint)kCellPx + tid.xy;
  if (pixel.x < width && pixel.y < height) {
    const float4 now = frame_tex.Load(int3(pixel, 0));
    const float4 before = previous_tex.Load(int3(pixel, 0));
    if (any(now != before)) InterlockedOr(g_changed, 1u);
    const float luma = Luma(now);
    if (luma >= bright_threshold) InterlockedAdd(g_bright, 1u);
    InterlockedAdd(g_luma, (uint)(min(luma, kLumaMax) * kLumaFixed + 0.5));
    const uint4 bits = asuint(now);
    const uint h = Pcg(bits.r ^ Pcg(bits.g ^ Pcg(bits.b ^ Pcg(index))));
    InterlockedAdd(g_hash_add, h);
    InterlockedXor(g_hash_xor, Pcg(h));
  }
  GroupMemoryBarrierWithGroupSync();

  if (index == 0) {
    const uint w = min(width - cell.x * (uint)kCellPx, (uint)kCellPx);
    const uint h = min(height - cell.y * (uint)kCellPx, (uint)kCellPx);
    const bool bright = (float)g_bright >= kBrightPixelFraction * (float)(w * h);
    const uint hash = g_hash_add ^ (g_hash_xor * 2654435761u);
    if (flags & kFlagFirstFrame) {
      // Ripresa dopo una pausa: stessa firma, stessa eta' di prima.
      const bool same = (flags & kFlagRestore) != 0 && saved_hash[cell.xy] == hash;
      const float since = same ? now_s - saved_age[cell.xy] : now_s;
      last_change_out[cell.xy] = since;
      anchor_hash_io[cell.xy] = hash;
      anchor_time_io[cell.xy] = since;
      excursion_io[cell.xy] = -1.0;
    } else if (hash == anchor_hash_io[cell.xy]) {
      const float excursion = excursion_io[cell.xy];
      if (excursion >= 0) {
        // Tornata com'era: dopo una deviazione breve riprende l'eta', altrimenti e' nuova.
        const bool brief = now_s - excursion <= kBlipS;
        last_change_out[cell.xy] = brief ? anchor_time_io[cell.xy] : now_s;
        if (!brief) anchor_time_io[cell.xy] = now_s;
        excursion_io[cell.xy] = -1.0;
      }
    } else if (g_changed != 0) {
      last_change_out[cell.xy] = now_s;
      const float excursion = excursion_io[cell.xy];
      if (excursion < 0) {
        excursion_io[cell.xy] = now_s;
      } else if (now_s - excursion > kBlipS) {
        anchor_hash_io[cell.xy] = hash;  // contenuto nuovo da troppo tempo: nuovo riferimento
        anchor_time_io[cell.xy] = now_s;
        excursion_io[cell.xy] = -1.0;
      }
    }
    flags_out[cell.xy] = bright ? 1u : 0u;
    hash_out[cell.xy] = hash;
    luma_out[cell.xy] = (float)g_luma / kLumaFixed / (float)(w * h);
    if (flags & kFlagFirstFrame) content_change_out[cell.xy] = last_change_out[cell.xy];
    else if (g_changed != 0) content_change_out[cell.xy] = now_s;
  }
}

// ------------------------------------------------------------------ finestre attive

RWTexture2D<float> active_last_change : register(u0);
RWTexture2D<float> active_anchor_time : register(u1);
RWTexture2D<float> active_excursion : register(u2);
Texture2D<uint> active_zones_in : register(t3);

// La finestra attiva (sotto il mouse o in primo piano) e' "cambiata adesso": torna
// normale con il ritorno rapido e il suo conteggio riparte quando smette di esserlo.
[numthreads(8, 8, 1)]
void CSActivate(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const uint zone = active_zones_in[id.xy];
  if (zone == 0 || (zone != active0 && zone != active1)) return;
  active_last_change[id.xy] = now_s;
  active_anchor_time[id.xy] = now_s;
  active_excursion[id.xy] = -1.0;
}

// ------------------------------------------------------------------ movimento intorno

Texture2D<uint> flags_in : register(t0);
Texture2D<float> last_change_in : register(t1);
Texture2D<float2> sums_in : register(t0);
Texture2D<float> motion_in : register(t2);
Texture2D<uint> zones_in : register(t3);
RWTexture2D<float2> sums_out : register(u0);
RWTexture2D<float> motion_out : register(u0);
RWTexture2D<float> levels_io : register(u0);
// Bit 0-1: ultima classe (0 nessuna, 1 elemento, 2 da non toccare);
// bit 2: ritorno lento in corso (classe cambiata con il livello sopra il bersaglio).
RWTexture2D<uint> class_io : register(u1);

// Quota di celle cambiate da poco nella finestra 33x33, contando solo la stessa zona della
// cella centrale (prima per righe, poi per colonne: come ZoneWindowMean del modello).
[numthreads(8, 8, 1)]
void CSMotionH(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const uint zone = zones_in[id.xy];
  const int x0 = max(0, (int)id.x - kRadius);
  const int x1 = min((int)cols - 1, (int)id.x + kRadius);
  float2 s = 0;
  for (int x = x0; x <= x1; ++x) {
    const int2 c = int2(x, id.y);
    if (zones_in[c] != zone) continue;
    s += float2(now_s - last_change_in[c] < kMotionWindowS ? 1.0 : 0.0, 1.0);
  }
  sums_out[id.xy] = s;
}

[numthreads(8, 8, 1)]
void CSMotionV(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const uint zone = zones_in[id.xy];
  const int y0 = max(0, (int)id.y - kRadius);
  const int y1 = min((int)rows - 1, (int)id.y + kRadius);
  float2 s = 0;
  for (int y = y0; y <= y1; ++y) {
    const int2 c = int2(id.x, y);
    if (zones_in[c] == zone) s += sums_in[c];
  }
  motion_out[id.xy] = s.x / s.y;  // s.y >= 1: la cella centrale conta sempre
}

// ------------------------------------------------------------------ livello

[numthreads(8, 8, 1)]
void CSLevel(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const int2 c = int2(id.xy);
  const float age = now_s - last_change_in[c];
  const bool bright = (flags_in[c] & 1u) != 0;
  const bool m = bright && age >= delay_s;
  // Desktop e barra: ogni elemento fermo e luminoso. Dentro un'app: solo con movimento intorno.
  const bool large =
      zones_in[c] != kZoneShell && (flags & kFlagWindowsLikeDesktop) == 0 && motion_in[c] < motion_fraction;
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

// ------------------------------------------------------------------ riassunto dei segnali

// Stesse costanti di src/core/summary_model.h.
static const uint kSummarySlots = 64;
static const uint kSummaryTotals = 6;
static const uint kSumRiskPx = 0;
static const uint kSumUnprotectedPx = 1;
static const uint kSumMotionPx = 2;
static const uint kSumLuma = 3;
static const uint kSumStillBefore = 4;
static const uint kSumStillRemoved = 5;
static const float kSummaryScale = 64.0;
static const uint kZoneCols = 32;
static const uint kZoneRows = 18;
static const float kStillS = 60.0;
static const float kMovingS = 10.0;
static const float kProtectedLevel = 0.05;

Texture2D<uint> summary_flags_in : register(t0);
Texture2D<float> summary_luma_in : register(t1);
Texture2D<float> summary_change_in : register(t2);
Texture2D<float> summary_levels_in : register(t3);
RWByteAddressBuffer summary_out : register(u0);

float ShadeEdge(float d, float s) {
  if (s <= 0.0) return 0.0;
  const float t = saturate(d / s);
  return 1.0 - t * t * (3.0 - 2.0 * t);
}

// Fattore di luminosita' e bordi, come ShadeFactor (src/core/shade_model.cpp).
float ShadeAt(float2 uv) {
  const float v = max(max(ShadeEdge(uv.x, shade_spread.x), ShadeEdge(1.0 - uv.x, shade_spread.y)),
                      max(ShadeEdge(uv.y, shade_spread.z), ShadeEdge(1.0 - uv.y, shade_spread.w)));
  return shade_brightness * (1.0 - shade_strength * v);
}

void AddTotal(uint slot, uint field, uint value) {
  if (value != 0) summary_out.InterlockedAdd((slot * kSummaryTotals + field) * 4, value);
}

// Una cella per thread: somme a virgola fissa nel buffer (azzerato prima del passaggio).
[numthreads(8, 8, 1)]
void CSSummary(uint3 id : SV_DispatchThreadID) {
  if (id.x >= cols || id.y >= rows) return;
  const uint x0 = id.x * kCellPx, y0 = id.y * kCellPx;
  const uint x1 = min(width, x0 + kCellPx), y1 = min(height, y0 + kCellPx);
  const uint px = (x1 - x0) * (y1 - y0);
  const float luma = summary_luma_in[id.xy];
  const float age = now_s - summary_change_in[id.xy];
  const bool still = age >= kStillS;
  const float level = summary_levels_in[id.xy];
  const float2 uv = float2((x0 + x1) * 0.5 / (float)width, (y0 + y1) * 0.5 / (float)height);
  const float arrived = luma * (1.0 - level) * ShadeAt(uv);
  const float scale = (float)px * kSummaryScale;
  const uint slot = (id.y * cols + id.x) % kSummarySlots;
  if (still && (summary_flags_in[id.xy] & 1u) != 0) {
    AddTotal(slot, kSumRiskPx, px);
    if (level <= kProtectedLevel) AddTotal(slot, kSumUnprotectedPx, px);
  }
  if (age < kMovingS) AddTotal(slot, kSumMotionPx, px);
  AddTotal(slot, kSumLuma, (uint)(luma * scale + 0.5));
  if (still) {
    AddTotal(slot, kSumStillBefore, (uint)(luma * scale + 0.5));
    AddTotal(slot, kSumStillRemoved, (uint)(max(0.0, luma - arrived) * scale + 0.5));
  }
  const uint zx = min(kZoneCols - 1, id.x * kZoneCols / cols);
  const uint zy = min(kZoneRows - 1, id.y * kZoneRows / rows);
  const uint zone_base = (kSummarySlots * kSummaryTotals + (zy * kZoneCols + zx) * 2) * 4;
  const uint light = (uint)(arrived * scale + 0.5);
  if (light != 0) summary_out.InterlockedAdd(zone_base, light);
  summary_out.InterlockedAdd(zone_base + 4, px);
}
