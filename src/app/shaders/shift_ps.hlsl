// Copia esatta pixel per pixel del desktop catturato, spostata di un offset intero.
// I bordi scoperti ripetono il pixel di bordo (clamp), niente nero e niente filtri.
Texture2D<float4> desktop : register(t0);

cbuffer ShiftConstants : register(b0) {
  int2 offset;
  uint2 size_minus_one;
};

float4 main(float4 pos : SV_Position) : SV_Target {
  int2 src = clamp(int2(pos.xy) - offset, int2(0, 0), int2(size_minus_one));
  return desktop.Load(int3(src, 0));
}
