#include "dimmer.h"

#include <algorithm>
#include <cstring>

#include "dim_detect.h"
#include "dim_level.h"
#include "dim_motion_h.h"
#include "dim_motion_v.h"

namespace oled {
namespace {

// Specchio del cbuffer DimConstants di dim_cs.hlsl.
struct DimConstants {
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
  float sdr_white;
  unsigned flags;
  unsigned cols;
  unsigned rows;
  unsigned width;
  unsigned height;
};
static_assert(sizeof(DimConstants) == 64);

constexpr unsigned kFlagSdr = 1;
constexpr unsigned kFlagSuspended = 2;
constexpr unsigned kFlagEnabled = 4;
constexpr unsigned kFlagLargeEnabled = 8;
constexpr unsigned kFlagFirstFrame = 16;
constexpr unsigned kFlagRestore = 32;

}  // namespace

HRESULT Dimmer::CreateGrid(DXGI_FORMAT format, const void* initial, UINT pixel_bytes, Grid& grid) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = cols_;
  desc.Height = rows_;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
  D3D11_SUBRESOURCE_DATA data{initial, cols_ * pixel_bytes, 0};
  HRESULT hr = device_->CreateTexture2D(&desc, initial ? &data : nullptr, &grid.texture);
  if (FAILED(hr)) return hr;
  hr = device_->CreateShaderResourceView(grid.texture.Get(), nullptr, &grid.srv);
  if (FAILED(hr)) return hr;
  return device_->CreateUnorderedAccessView(grid.texture.Get(), nullptr, &grid.uav);
}

HRESULT Dimmer::Init(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, double now_ms,
                     const DimSavedState* restore) {
  Shutdown();
  device_ = device;
  device_->GetImmediateContext(&context_);
  width_ = width;
  height_ = height;
  cols_ = (width + kDimCellPx - 1) / kDimCellPx;
  rows_ = (height + kDimCellPx - 1) / kDimCellPx;
  sdr_ = format != DXGI_FORMAT_R16G16B16A16_FLOAT;
  epoch_ms_ = now_ms;
  first_frame_ = true;
  last_update_s_ = -1;

  HRESULT hr = S_OK;
  const auto shader = [&](const BYTE* code, size_t size, ComPtr<ID3D11ComputeShader>& out) {
    if (SUCCEEDED(hr)) hr = device_->CreateComputeShader(code, size, nullptr, &out);
  };
  shader(g_dim_detect, sizeof(g_dim_detect), detect_);
  shader(g_dim_motion_h, sizeof(g_dim_motion_h), motion_h_);
  shader(g_dim_motion_v, sizeof(g_dim_motion_v), motion_v_);
  shader(g_dim_level, sizeof(g_dim_level), level_);
  if (FAILED(hr)) return hr;

  D3D11_BUFFER_DESC cb{};
  cb.ByteWidth = sizeof(DimConstants);
  cb.Usage = D3D11_USAGE_DEFAULT;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  hr = device_->CreateBuffer(&cb, nullptr, &constants_);
  if (FAILED(hr)) return hr;

  // Tutte le celle "cambiate" all'istante 0 della sessione, livello 0, zona "app".
  const size_t n = static_cast<size_t>(cols_) * rows_;
  const std::vector<float> zeros(n, 0.0f);
  const std::vector<uint32_t> no_flags(n, 0u);
  const std::vector<uint32_t> app_zones(n, kDimZoneApp);
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, zeros.data(), 4, last_change_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, no_flags.data(), 4, flags_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, no_flags.data(), 4, hash_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32G32_FLOAT, nullptr, 8, sums_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, nullptr, 4, motion_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, zeros.data(), 4, levels_grid_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, no_flags.data(), 4, class_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, app_zones.data(), 4, zones_))) return hr;
  const std::vector<float> no_excursion(n, -1.0f);
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, no_flags.data(), 4, anchor_hash_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, zeros.data(), 4, anchor_time_))) return hr;
  if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, no_excursion.data(), 4, excursion_))) return hr;
  // Ripresa dopo una pausa: solo con le stesse dimensioni.
  restore_ = restore && restore->width == static_cast<int>(width) && restore->height == static_cast<int>(height) &&
             restore->ages.size() == n && restore->hashes.size() == n;
  if (restore_) {
    pending_restore_ = *restore;
    if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_UINT, restore->hashes.data(), 4, saved_hash_))) return hr;
    if (FAILED(hr = CreateGrid(DXGI_FORMAT_R32_FLOAT, restore->ages.data(), 4, saved_age_))) return hr;
  }
  levels_srv_ = levels_grid_.srv;
  levels_ = levels_grid_.texture.Get();
  return S_OK;
}

void Dimmer::Shutdown() {
  levels_ = nullptr;
  levels_srv_.Reset();
  for (Grid* g : {&last_change_, &flags_, &hash_, &sums_, &motion_, &levels_grid_, &class_, &zones_, &saved_hash_,
                  &saved_age_, &anchor_hash_, &anchor_time_, &excursion_}) {
    *g = Grid{};
  }
  detect_.Reset();
  motion_h_.Reset();
  motion_v_.Reset();
  level_.Reset();
  constants_.Reset();
  frame_copy_.Reset();
  previous_srv_.Reset();
  previous_srv_source_.Reset();
  staging_.Reset();
  readback_pending_ = false;
  for (auto& s : debug_staging_) s.Reset();
  debug_pending_ = false;
  restore_ = false;
  pending_restore_ = DimSavedState{};
  context_.Reset();
  device_.Reset();
  cols_ = rows_ = width_ = height_ = 0;
}

void Dimmer::SetZones(const std::vector<uint8_t>& zones) {
  if (!Ready() || zones.size() != static_cast<size_t>(cols_) * rows_) return;
  const std::vector<uint32_t> wide(zones.begin(), zones.end());
  context_->UpdateSubresource(zones_.texture.Get(), 0, nullptr, wide.data(), cols_ * 4, 0);
}

void Dimmer::SetConstants(double now_s, double dt_s, const DimParams& p, float sdr_white, bool suspended,
                          bool first) {
  DimConstants c{};
  c.now_s = static_cast<float>(now_s);
  c.dt_s = static_cast<float>(dt_s);
  c.strength = static_cast<float>(p.strength);
  c.delay_s = static_cast<float>(p.delay_s);
  c.ramp_s = static_cast<float>(p.ramp_s);
  c.recover_s = static_cast<float>(p.recover_s);
  c.large_delay_s = static_cast<float>(p.large_delay_s);
  c.large_strength = static_cast<float>(p.large_strength);
  c.bright_threshold = static_cast<float>(p.bright_threshold);
  c.motion_fraction = static_cast<float>(p.motion_fraction);
  c.sdr_white = sdr_white > 0 ? sdr_white : 1.0f;
  c.flags = (sdr_ ? kFlagSdr : 0) | (suspended ? kFlagSuspended : 0) | (p.enabled ? kFlagEnabled : 0) |
            (p.large_enabled ? kFlagLargeEnabled : 0) | (first ? kFlagFirstFrame : 0) |
            (first && restore_ ? kFlagRestore : 0);
  c.cols = cols_;
  c.rows = rows_;
  c.width = width_;
  c.height = height_;
  context_->UpdateSubresource(constants_.Get(), 0, nullptr, &c, 0, 0);
}

void Dimmer::Run(ID3D11ComputeShader* shader, std::initializer_list<ID3D11ShaderResourceView*> srvs,
                 std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT groups_x, UINT groups_y) {
  ID3D11ShaderResourceView* srv_list[4] = {};
  ID3D11UnorderedAccessView* uav_list[6] = {};
  UINT n = 0;
  for (auto* s : srvs) srv_list[n++] = s;
  n = 0;
  for (auto* u : uavs) uav_list[n++] = u;
  ID3D11Buffer* cb = constants_.Get();
  context_->CSSetShader(shader, nullptr, 0);
  context_->CSSetConstantBuffers(0, 1, &cb);
  context_->CSSetShaderResources(0, 4, srv_list);
  context_->CSSetUnorderedAccessViews(0, 6, uav_list, nullptr);
  context_->Dispatch(groups_x, groups_y, 1);
  // Si staccano subito: la stessa texture fa da uscita e poi da ingresso.
  ID3D11ShaderResourceView* no_srv[4] = {};
  ID3D11UnorderedAccessView* no_uav[6] = {};
  context_->CSSetShaderResources(0, 4, no_srv);
  context_->CSSetUnorderedAccessViews(0, 6, no_uav, nullptr);
  context_->CSSetShader(nullptr, nullptr, 0);
}

HRESULT Dimmer::Detect(ID3D11Texture2D* frame, ID3D11Texture2D* previous_cache, double now_ms, const DimParams& p,
                       float sdr_white) {
  if (!Ready()) return E_UNEXPECTED;
  last_white_ = sdr_white;
  ComPtr<ID3D11ShaderResourceView> frame_srv;
  HRESULT hr = device_->CreateShaderResourceView(frame, nullptr, &frame_srv);
  if (FAILED(hr)) {
    // Ripiego: il frame acquisito non e' leggibile dagli shader, se ne fa una copia.
    if (!frame_copy_) {
      D3D11_TEXTURE2D_DESC desc{};
      frame->GetDesc(&desc);
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      hr = device_->CreateTexture2D(&desc, nullptr, &frame_copy_);
      if (FAILED(hr)) return hr;
    }
    context_->CopyResource(frame_copy_.Get(), frame);
    hr = device_->CreateShaderResourceView(frame_copy_.Get(), nullptr, &frame_srv);
    if (FAILED(hr)) return hr;
  }
  if (previous_srv_source_.Get() != previous_cache) {
    previous_srv_.Reset();
    hr = device_->CreateShaderResourceView(previous_cache, nullptr, &previous_srv_);
    if (FAILED(hr)) return hr;
    previous_srv_source_ = previous_cache;
  }
  SetConstants(Seconds(now_ms), 0, p, sdr_white, false, first_frame_);
  Run(detect_.Get(), {frame_srv.Get(), previous_srv_.Get(), saved_hash_.srv.Get(), saved_age_.srv.Get()},
      {last_change_.uav.Get(), flags_.uav.Get(), hash_.uav.Get(), anchor_hash_.uav.Get(), anchor_time_.uav.Get(),
       excursion_.uav.Get()},
      cols_, rows_);
  if (first_frame_ && restore_) {
    saved_hash_ = Grid{};
    saved_age_ = Grid{};
    restore_ = false;
    pending_restore_ = DimSavedState{};
  }
  first_frame_ = false;
  return S_OK;
}

void Dimmer::Update(double now_ms, const DimParams& p, bool suspended) {
  if (!Ready()) return;
  const double now_s = Seconds(now_ms);
  const double dt = last_update_s_ < 0 ? 0.0 : std::max(0.0, now_s - last_update_s_);
  last_update_s_ = now_s;
  SetConstants(now_s, dt, p, last_white_, suspended, false);
  const UINT gx = (cols_ + 7) / 8, gy = (rows_ + 7) / 8;
  Run(motion_h_.Get(), {nullptr, last_change_.srv.Get(), nullptr, zones_.srv.Get()}, {sums_.uav.Get()}, gx, gy);
  Run(motion_v_.Get(), {sums_.srv.Get(), nullptr, nullptr, zones_.srv.Get()}, {motion_.uav.Get()}, gx, gy);
  Run(level_.Get(), {flags_.srv.Get(), last_change_.srv.Get(), motion_.srv.Get(), zones_.srv.Get()},
      {levels_grid_.uav.Get(), class_.uav.Get()}, gx, gy);
}

std::vector<uint8_t> Dimmer::ReadGridBlocking(ID3D11Texture2D* texture) {
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  std::vector<uint8_t> out;
  if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) return out;
  context_->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return out;
  out.resize(static_cast<size_t>(cols_) * rows_ * 4);
  for (UINT y = 0; y < rows_; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * cols_ * 4,
                static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch, cols_ * 4);
  }
  context_->Unmap(staging.Get(), 0);
  return out;
}

bool Dimmer::SaveState(double now_ms, DimSavedState& out) {
  if (!Ready()) return false;
  if (first_frame_) {
    // Nessun frame ancora: si restituisce lo stato ricevuto, se c'era (es. cattura persa
    // subito dopo il riavvio), altrimenti le firme non valgono.
    if (!restore_) return false;
    out = pending_restore_;
    return true;
  }
  const auto last_change = ReadGridBlocking(last_change_.texture.Get());
  const auto hashes = ReadGridBlocking(hash_.texture.Get());
  const auto anchor_hash = ReadGridBlocking(anchor_hash_.texture.Get());
  const auto anchor_time = ReadGridBlocking(anchor_time_.texture.Get());
  const auto excursion = ReadGridBlocking(excursion_.texture.Get());
  const size_t n = static_cast<size_t>(cols_) * rows_;
  for (const auto* grid : {&last_change, &hashes, &anchor_hash, &anchor_time, &excursion}) {
    if (grid->size() != n * 4) return false;
  }
  const auto read_f = [](const std::vector<uint8_t>& g, size_t i) {
    float v = 0;
    std::memcpy(&v, g.data() + i * 4, 4);
    return v;
  };
  const auto read_u = [](const std::vector<uint8_t>& g, size_t i) {
    uint32_t v = 0;
    std::memcpy(&v, g.data() + i * 4, 4);
    return v;
  };
  out.width = static_cast<int>(width_);
  out.height = static_cast<int>(height_);
  out.ages.resize(n);
  out.hashes.resize(n);
  const float now_s = static_cast<float>(Seconds(now_ms));
  for (size_t i = 0; i < n; ++i) {
    // Durante una deviazione breve vale il contenuto di riferimento (es. icona sotto il mouse).
    const float exc = read_f(excursion, i);
    const bool brief = exc >= 0 && now_s - exc <= static_cast<float>(kDimBlipS);
    out.hashes[i] = brief ? read_u(anchor_hash, i) : read_u(hashes, i);
    out.ages[i] = std::max(0.0f, now_s - (brief ? read_f(anchor_time, i) : read_f(last_change, i)));
  }
  return true;
}

void Dimmer::RequestReadback() {
  if (!Ready()) return;
  if (!staging_) {
    D3D11_TEXTURE2D_DESC desc{};
    levels_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_))) return;
  }
  context_->CopyResource(staging_.Get(), levels_);
  context_->Flush();  // senza, la copia potrebbe non partire finche' non serve
  readback_pending_ = true;
}

bool Dimmer::TryReadback(std::vector<float>& levels) {
  if (!readback_pending_ || !staging_) return false;
  D3D11_MAPPED_SUBRESOURCE map{};
  // Mai attendere la GPU: se la copia non e' pronta si riprova al giro dopo.
  const HRESULT hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
  if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
  readback_pending_ = false;
  if (FAILED(hr)) return false;
  levels.resize(static_cast<size_t>(cols_) * rows_);
  for (UINT y = 0; y < rows_; ++y) {
    const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(map.pData) + y * map.RowPitch);
    std::copy(row, row + cols_, levels.begin() + static_cast<size_t>(y) * cols_);
  }
  context_->Unmap(staging_.Get(), 0);
  return true;
}

void Dimmer::RequestDebugReadback(double now_ms) {
  if (!Ready() || debug_pending_) return;
  ID3D11Texture2D* sources[4] = {levels_, last_change_.texture.Get(), flags_.texture.Get(), class_.texture.Get()};
  for (int i = 0; i < 4; ++i) {
    if (!debug_staging_[i]) {
      D3D11_TEXTURE2D_DESC desc{};
      sources[i]->GetDesc(&desc);
      desc.Usage = D3D11_USAGE_STAGING;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.MiscFlags = 0;
      if (FAILED(device_->CreateTexture2D(&desc, nullptr, &debug_staging_[i]))) return;
    }
    context_->CopyResource(debug_staging_[i].Get(), sources[i]);
  }
  context_->Flush();
  debug_now_s_ = Seconds(now_ms);
  debug_pending_ = true;
}

bool Dimmer::TryDebugReadback(DimDebugGrids& out) {
  if (!debug_pending_) return false;
  D3D11_MAPPED_SUBRESOURCE maps[4] = {};
  int mapped = 0;
  for (; mapped < 4; ++mapped) {
    const HRESULT hr =
        context_->Map(debug_staging_[mapped].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &maps[mapped]);
    if (FAILED(hr)) break;
  }
  if (mapped < 4) {
    for (int i = 0; i < mapped; ++i) context_->Unmap(debug_staging_[i].Get(), 0);
    return false;  // non ancora pronte: si riprova al giro dopo
  }
  const size_t n = static_cast<size_t>(cols_) * rows_;
  out.levels.resize(n);
  out.last_change.resize(n);
  out.flags.resize(n);
  out.classes.resize(n);
  void* dst[4] = {out.levels.data(), out.last_change.data(), out.flags.data(), out.classes.data()};
  for (int i = 0; i < 4; ++i) {
    for (UINT y = 0; y < rows_; ++y) {
      std::memcpy(static_cast<uint8_t*>(dst[i]) + static_cast<size_t>(y) * cols_ * 4,
                  static_cast<const uint8_t*>(maps[i].pData) + static_cast<size_t>(y) * maps[i].RowPitch, cols_ * 4);
    }
    context_->Unmap(debug_staging_[i].Get(), 0);
  }
  out.now_s = debug_now_s_;
  debug_pending_ = false;
  return true;
}

}  // namespace oled
