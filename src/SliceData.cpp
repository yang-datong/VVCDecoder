#include "SliceData.hpp"
#include "Cabac.hpp"
#include "Common.hpp"
#include "Frame.hpp"
#include "GOP.hpp"
#include "MacroBlock.hpp"
#include "Nalu.hpp"
#include "PU.hpp"
#include "PictureBase.hpp"
#include "Type.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

static inline int clip_int16_value(int value) {
  return Clip3(-32768, 32767, value);
}

static bool vvc_force_root_split_enabled() {
  const char *env = std::getenv("VVC_FORCE_ROOT_SPLIT");
  if ((!env || !*env)) env = std::getenv("HEVC_FORCE_ROOT_SPLIT");
  if (!env || !*env) return false;
  return std::atoi(env) != 0;
}

static inline int hevc_local_tb_addr_zs(const SPS *sps, int x_tb, int y_tb) {
  if (!sps) return 0;
  int p = 0;
  for (int i = 0, m = 1; m <= sps->tb_mask; ++i, m <<= 1)
    p += ((x_tb & m) ? (m * m) : 0) + ((y_tb & m) ? (2 * m * m) : 0);
  return p;
}

static inline int hevc_min_tb_addr_zs(const PPS *pps, const SPS *sps, int x_tb,
                                      int y_tb) {
  if (pps) {
    const int mapped = pps->minTbAddrZs(x_tb, y_tb);
    if (mapped >= 0) return mapped;
  }
  if (x_tb < 0 || y_tb < 0) return -1;
  return hevc_local_tb_addr_zs(sps, x_tb, y_tb);
}

static inline int hevc_z_scan_block_avail(const PPS *pps, const SPS *sps,
                                          int x_curr, int y_curr, int x_nb,
                                          int y_nb) {
  if (!sps || x_nb < 0 || y_nb < 0) return 0;

  const int x_curr_ctb = x_curr >> sps->CtbLog2SizeY;
  const int y_curr_ctb = y_curr >> sps->CtbLog2SizeY;
  const int x_nb_ctb = x_nb >> sps->CtbLog2SizeY;
  const int y_nb_ctb = y_nb >> sps->CtbLog2SizeY;
  if (y_nb_ctb < y_curr_ctb || x_nb_ctb < x_curr_ctb) return 1;

  const int min_tb_log2 = sps->log2_min_luma_transform_block_size;
  const int curr = hevc_min_tb_addr_zs(
      pps, sps, (x_curr >> min_tb_log2) & sps->tb_mask,
      (y_curr >> min_tb_log2) & sps->tb_mask);
  const int nb = hevc_min_tb_addr_zs(
      pps, sps, (x_nb >> min_tb_log2) & sps->tb_mask,
      (y_nb >> min_tb_log2) & sps->tb_mask);
  return nb <= curr;
}

static const int8_t kHevcTransform[32][32] = {
    {64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64},
    {90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13, 4,
     -4, -13, -22, -31, -38, -46, -54, -61, -67, -73, -78, -82, -85, -88,
     -90, -90},
    {90, 87, 80, 70, 57, 43, 25, 9, -9, -25, -43, -57, -70, -80, -87, -90,
     -90, -87, -80, -70, -57, -43, -25, -9, 9, 25, 43, 57, 70, 80, 87, 90},
    {90, 82, 67, 46, 22, -4, -31, -54, -73, -85, -90, -88, -78, -61, -38,
     -13, 13, 38, 61, 78, 88, 90, 85, 73, 54, 31, 4, -22, -46, -67, -82,
     -90},
    {89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89,
     89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89},
    {88, 67, 31, -13, -54, -82, -90, -78, -46, -4, 38, 73, 90, 85, 61, 22,
     -22, -61, -85, -90, -73, -38, 4, 46, 78, 90, 82, 54, 13, -31, -67, -88},
    {87, 57, 9, -43, -80, -90, -70, -25, 25, 70, 90, 80, 43, -9, -57, -87,
     -87, -57, -9, 43, 80, 90, 70, 25, -25, -70, -90, -80, -43, 9, 57, 87},
    {85, 46, -13, -67, -90, -73, -22, 38, 82, 88, 54, -4, -61, -90, -78, -31,
     31, 78, 90, 61, 4, -54, -88, -82, -38, 22, 73, 90, 67, 13, -46, -85},
    {83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83,
     83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83},
    {82, 22, -54, -90, -61, 13, 78, 85, 31, -46, -90, -67, 4, 73, 88, 38, -38,
     -88, -73, -4, 67, 90, 46, -31, -85, -78, -13, 61, 90, 54, -22, -82},
    {80, 9, -70, -87, -25, 57, 90, 43, -43, -90, -57, 25, 87, 70, -9, -80,
     -80, -9, 70, 87, 25, -57, -90, -43, 43, 90, 57, -25, -87, -70, 9, 80},
    {78, -4, -82, -73, 13, 85, 67, -22, -88, -61, 31, 90, 54, -38, -90, -46,
     46, 90, 38, -54, -90, -31, 61, 88, 22, -67, -85, -13, 73, 82, 4, -78},
    {75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75,
     75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75},
    {73, -31, -90, -22, 78, 67, -38, -90, -13, 82, 61, -46, -88, -4, 85, 54,
     -54, -85, 4, 88, 46, -61, -82, 13, 90, 38, -67, -78, 22, 90, 31, -73},
    {70, -43, -87, 9, 90, 25, -80, -57, 57, 80, -25, -90, -9, 87, 43, -70,
     -70, 43, 87, -9, -90, -25, 80, 57, -57, -80, 25, 90, 9, -87, -43, 70},
    {67, -54, -78, 38, 85, -22, -90, 4, 90, 13, -88, -31, 82, 46, -73, -61,
     61, 73, -46, -82, 31, 88, -13, -90, -4, 90, 22, -85, -38, 78, 54, -67},
    {64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64,
     64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64},
    {61, -73, -46, 82, 31, -88, -13, 90, -4, -90, 22, 85, -38, -78, 54, 67,
     -67, -54, 78, 38, -85, -22, 90, 4, -90, 13, 88, -31, -82, 46, 73, -61},
    {57, -80, -25, 90, -9, -87, 43, 70, -70, -43, 87, 9, -90, 25, 80, -57,
     -57, 80, 25, -90, 9, 87, -43, -70, 70, 43, -87, -9, 90, -25, -80, 57},
    {54, -85, -4, 88, -46, -61, 82, 13, -90, 38, 67, -78, -22, 90, -31, -73,
     73, 31, -90, 22, 78, -67, -38, 90, -13, -82, 61, 46, -88, 4, 85, -54},
    {50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50,
     50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50},
    {46, -90, 38, 54, -90, 31, 61, -88, 22, 67, -85, 13, 73, -82, 4, 78, -78,
     -4, 82, -73, -13, 85, -67, -22, 88, -61, -31, 90, -54, -38, 90, -46},
    {43, -90, 57, 25, -87, 70, 9, -80, 80, -9, -70, 87, -25, -57, 90, -43,
     -43, 90, -57, -25, 87, -70, -9, 80, -80, 9, 70, -87, 25, 57, -90, 43},
    {38, -88, 73, -4, -67, 90, -46, -31, 85, -78, 13, 61, -90, 54, 22, -82,
     82, -22, -54, 90, -61, -13, 78, -85, 31, 46, -90, 67, 4, -73, 88, -38},
    {36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36,
     36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36},
    {31, -78, 90, -61, 4, 54, -88, 82, -38, -22, 73, -90, 67, -13, -46, 85,
     -85, 46, 13, -67, 90, -73, 22, 38, -82, 88, -54, -4, 61, -90, 78, -31},
    {25, -70, 90, -80, 43, 9, -57, 87, -87, 57, -9, -43, 80, -90, 70, -25,
     -25, 70, -90, 80, -43, -9, 57, -87, 87, -57, 9, 43, -80, 90, -70, 25},
    {22, -61, 85, -90, 73, -38, -4, 46, -78, 90, -82, 54, -13, -31, 67, -88,
     88, -67, 31, 13, -54, 82, -90, 78, -46, 4, 38, -73, 90, -85, 61, -22},
    {18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18,
     18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18},
    {13, -38, 61, -78, 88, -90, 85, -73, 54, -31, 4, 22, -46, 67, -82, 90,
     -90, 82, -67, 46, -22, -4, 31, -54, 73, -85, 90, -88, 78, -61, 38, -13},
    {9, -25, 43, -57, 70, -80, 87, -90, 90, -87, 80, -70, 57, -43, 25, -9,
     -9, 25, -43, 57, -70, 80, -87, 90, -90, 87, -80, 70, -57, 43, -25, 9},
    {4, -13, 22, -31, 38, -46, 54, -61, 67, -73, 78, -82, 85, -88, 90, -90,
     90, -90, 88, -85, 82, -78, 73, -67, 61, -54, 46, -38, 31, -22, 13, -4},
};

static void hevc_inverse_dct_2d(const std::vector<int32_t> &src, int n,
                                int bit_depth,
                                std::vector<int32_t> &dst) {
  std::vector<int32_t> tmp(n * n, 0);
  dst.assign(n * n, 0);
  const int step = 32 / n;

  const int first_shift = 7;
  const int first_add = 1 << (first_shift - 1);
  for (int x = 0; x < n; ++x) {
    for (int out_y = 0; out_y < n; ++out_y) {
      int64_t sum = 0;
      for (int k = 0; k < n; ++k)
        sum += static_cast<int64_t>(kHevcTransform[k * step][out_y]) *
               src[k * n + x];
      tmp[out_y * n + x] =
          clip_int16_value(static_cast<int>((sum + first_add) >> first_shift));
    }
  }

  const int second_shift = 20 - bit_depth;
  const int second_add = 1 << (second_shift - 1);
  for (int y = 0; y < n; ++y) {
    for (int out_x = 0; out_x < n; ++out_x) {
      int64_t sum = 0;
      for (int k = 0; k < n; ++k)
        sum += static_cast<int64_t>(kHevcTransform[k * step][out_x]) *
               tmp[y * n + k];
      dst[y * n + out_x] =
          clip_int16_value(static_cast<int>((sum + second_add) >> second_shift));
    }
  }
}

static void hevc_inverse_dst_4x4_luma(const std::vector<int32_t> &src,
                                      int bit_depth,
                                      std::vector<int32_t> &dst) {
  static const int kDst4[4][4] = {
      {29, 55, 74, 84},
      {74, 74, 0, -74},
      {84, -29, -74, 55},
      {55, -84, 74, -29},
  };

  std::vector<int32_t> tmp(16, 0);
  dst.assign(16, 0);

  const int first_shift = 7;
  const int first_add = 1 << (first_shift - 1);
  for (int x = 0; x < 4; ++x) {
    for (int out_y = 0; out_y < 4; ++out_y) {
      int64_t sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += static_cast<int64_t>(kDst4[k][out_y]) * src[k * 4 + x];
      tmp[out_y * 4 + x] =
          clip_int16_value(static_cast<int>((sum + first_add) >> first_shift));
    }
  }

  const int second_shift = 20 - bit_depth;
  const int second_add = 1 << (second_shift - 1);
  for (int y = 0; y < 4; ++y) {
    for (int out_x = 0; out_x < 4; ++out_x) {
      int64_t sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += static_cast<int64_t>(kDst4[k][out_x]) * tmp[y * 4 + k];
      dst[y * 4 + out_x] =
          clip_int16_value(static_cast<int>((sum + second_add) >> second_shift));
    }
  }
}

static void hevc_add_residual_to_luma(PictureBase *pic, int x0, int y0, int n,
                                      int bit_depth,
                                      const std::vector<int32_t> &residual) {
  if (!pic || !pic->m_pic_buff_luma) return;
  const int width = pic->PicWidthInSamplesL;
  const int height = pic->PicHeightInSamplesL;
  if (width <= 0 || height <= 0) return;

  for (int y = 0; y < n; ++y) {
    const int py = y0 + y;
    if (py < 0 || py >= height) continue;
    for (int x = 0; x < n; ++x) {
      const int px = x0 + x;
      if (px < 0 || px >= width) continue;
      const int idx = py * width + px;
      pic->m_pic_buff_luma[idx] = ClipBD(
          static_cast<int>(pic->m_pic_buff_luma[idx]) + residual[y * n + x],
          bit_depth);
    }
  }
}

static int hevc_derive_intra_chroma_pred_mode(int luma_mode, int chroma_mode) {
  static const uint8_t intra_chroma_table[4] = {0, 26, 10, 1};
  if (chroma_mode == 4) return luma_mode;
  if (chroma_mode < 0 || chroma_mode > 3) return intra_chroma_table[0];
  const int derived = intra_chroma_table[chroma_mode];
  return (luma_mode == derived) ? 34 : derived;
}

static int hevc_tab_mode_idx(int mode_idx) {
  static const uint8_t tab_mode_idx[] = {
      0,  1,  2,  2,  2,  2,  3,  5,  7,  8,  10, 12,
      13, 15, 17, 18, 19, 20, 21, 22, 23, 23, 24, 24,
      25, 25, 26, 27, 27, 28, 28, 29, 29, 30, 31};
  return tab_mode_idx[Clip3(0, 34, mode_idx)];
}

struct VvcEbspRbspByteMap {
  const uint8_t *raw_payload = nullptr;
  int raw_size = 0;
  std::vector<int> raw_to_rbsp;
  std::vector<int> rbsp_to_raw;

  bool valid() const {
    return raw_payload != nullptr && raw_size >= 0 && !raw_to_rbsp.empty() &&
           !rbsp_to_raw.empty();
  }
};

static VvcEbspRbspByteMap build_vvc_ebsp_rbsp_byte_map(const Nalu *nalu,
                                                       int rbsp_size) {
  VvcEbspRbspByteMap map;
  if (!nalu || !nalu->buffer || rbsp_size < 0) return map;

  constexpr int kNalHeaderBytes = 2;
  const int payload_offset = nalu->startCodeLenth + kNalHeaderBytes;
  if (payload_offset < 0 || payload_offset > nalu->len) return map;

  map.raw_payload = nalu->buffer + payload_offset;
  map.raw_size = nalu->len - payload_offset;
  map.raw_to_rbsp.assign(map.raw_size + 1, 0);
  map.rbsp_to_raw.assign(rbsp_size + 1, 0);
  map.raw_to_rbsp[0] = 0;
  map.rbsp_to_raw[0] = 0;

  int raw = 0;
  int rbsp = 0;
  while (raw < map.raw_size) {
    if (raw + 2 < map.raw_size && map.raw_payload[raw] == 0x00 &&
        map.raw_payload[raw + 1] == 0x00 && map.raw_payload[raw + 2] == 0x03) {
      if (rbsp + 2 > rbsp_size) {
        map.raw_to_rbsp.clear();
        map.rbsp_to_raw.clear();
        return map;
      }

      ++rbsp;
      map.raw_to_rbsp[raw + 1] = rbsp;
      map.rbsp_to_raw[rbsp] = raw + 1;

      ++rbsp;
      map.raw_to_rbsp[raw + 2] = rbsp;
      map.rbsp_to_raw[rbsp] = raw + 2;

      map.raw_to_rbsp[raw + 3] = rbsp;
      map.rbsp_to_raw[rbsp] = raw + 3;
      raw += 3;
      continue;
    }

    if (rbsp + 1 > rbsp_size) {
      map.raw_to_rbsp.clear();
      map.rbsp_to_raw.clear();
      return map;
    }

    ++rbsp;
    map.raw_to_rbsp[raw + 1] = rbsp;
    map.rbsp_to_raw[rbsp] = raw + 1;
    ++raw;
  }

  if (rbsp != rbsp_size) {
    map.raw_to_rbsp.clear();
    map.rbsp_to_raw.clear();
  }
  return map;
}

static bool build_wpp_row_starts_from_entry_points(
    const Nalu *nalu, const BitStream *bitstream,
    const uint8_t *slice_data_start, const uint8_t *slice_data_end,
    const SliceHeader *header, std::vector<const uint8_t *> &wpp_row_starts) {
  if (!nalu || !bitstream || !header || !slice_data_start || !slice_data_end)
    return false;

  const uint8_t *rbsp_start = bitstream->getBufStart();
  if (!rbsp_start || slice_data_start < rbsp_start || slice_data_end < rbsp_start)
    return false;

  const int rbsp_size = static_cast<int>(slice_data_end - rbsp_start);
  const auto map = build_vvc_ebsp_rbsp_byte_map(nalu, rbsp_size);
  if (!map.valid()) return false;

  const std::ptrdiff_t slice_data_rbsp_offset = slice_data_start - rbsp_start;
  if (slice_data_rbsp_offset < 0 ||
      slice_data_rbsp_offset >=
          static_cast<std::ptrdiff_t>(map.rbsp_to_raw.size()))
    return false;

  std::size_t raw_cursor =
      static_cast<std::size_t>(map.rbsp_to_raw[slice_data_rbsp_offset]);
  wpp_row_starts.reserve(header->num_entry_point_offsets + 1);
  wpp_row_starts.push_back(slice_data_start);

  for (int i = 0; i < header->num_entry_point_offsets; ++i) {
    const int step = header->entry_point_offset_minus1[i] + 1;
    if (step <= 0) {
      std::cerr << "WARN: invalid entry_point_offset at " << i << std::endl;
      return false;
    }
    if (raw_cursor + static_cast<std::size_t>(step) >
        static_cast<std::size_t>(map.raw_size)) {
      std::cerr << "WARN: entry_point_offset raw cursor out of range at " << i
                << " (step=" << step
                << ", remaining="
                << (static_cast<std::size_t>(map.raw_size) - raw_cursor)
                << ", num_entry_point_offsets="
                << header->num_entry_point_offsets << ")" << std::endl;
      return false;
    }

    raw_cursor += static_cast<std::size_t>(step);
    const int rbsp_offset = map.raw_to_rbsp[raw_cursor];
    if (rbsp_offset < 0 || rbsp_offset > rbsp_size) return false;

    const uint8_t *row_start = rbsp_start + rbsp_offset;
    if (row_start > slice_data_end) return false;
    wpp_row_starts.push_back(row_start);
  }

  return true;
}

int SliceData::state_x(int pixel_coord) const {
  if (!m_sps || m_sps->min_cb_width <= 0) return 0;
  const int idx = pixel_coord >> m_sps->log2_min_luma_coding_block_size;
  return Clip3(0, m_sps->min_cb_width - 1, idx);
}

int SliceData::state_y(int pixel_coord) const {
  if (!m_sps || m_sps->min_cb_height <= 0) return 0;
  const int idx = pixel_coord >> m_sps->log2_min_luma_coding_block_size;
  return Clip3(0, m_sps->min_cb_height - 1, idx);
}

void SliceData::reset_block_state_maps() {
  if (!m_sps) return;

  const int width = MAX(1, m_sps->min_cb_width);
  const int height = MAX(1, m_sps->min_cb_height);
  auto init_2d = [&](std::vector<std::vector<uint8_t>> &map) {
    map.assign(width, std::vector<uint8_t>(height, 0));
  };
  auto init_3d = [&](std::vector<std::vector<std::vector<uint8_t>>> &map) {
    map.assign(width,
               std::vector<std::vector<uint8_t>>(height,
                                                 std::vector<uint8_t>(64, 0)));
  };

  init_2d(CuPredMode);
  init_2d(cu_skip_flag);
  init_2d(merge_flag);
  init_2d(merge_idx);
  init_2d(rem_intra_luma_pred_mode);
  init_2d(intra_chroma_pred_mode);
  init_3d(split_transform_flag);
  init_3d(cbf_cb);
  init_3d(cbf_cr);
  init_3d(cbf_luma);
}

void SliceData::fill_block_map(std::vector<std::vector<uint8_t>> &map, int x0,
                               int y0, int width, int height, uint8_t value) {
  if (!m_sps || map.empty() || width <= 0 || height <= 0) return;

  const int min_cb_size = 1 << m_sps->log2_min_luma_coding_block_size;
  const int x_start = state_x(x0);
  const int y_start = state_y(y0);
  const int x_end = Clip3(0, m_sps->min_cb_width,
                          (x0 + width + min_cb_size - 1) >>
                              m_sps->log2_min_luma_coding_block_size);
  const int y_end = Clip3(0, m_sps->min_cb_height,
                          (y0 + height + min_cb_size - 1) >>
                              m_sps->log2_min_luma_coding_block_size);
  for (int x = x_start; x < x_end; ++x)
    for (int y = y_start; y < y_end; ++y) map[x][y] = value;
}

int SliceData::get_qPy_pred(int xBase, int yBase, int log2CbSize) {
  if (!m_sps || !m_pps) return header ? header->SliceQpY : 0;

  const int ctb_size_mask = (1 << m_sps->CtbLog2SizeY) - 1;
  const int min_cu_qp_delta_size_mask =
      (1 << (m_sps->CtbLog2SizeY - m_pps->diff_cu_qp_delta_depth)) - 1;
  const int xQgBase = xBase - (xBase & min_cu_qp_delta_size_mask);
  const int yQgBase = yBase - (yBase & min_cu_qp_delta_size_mask);
  const int x_cb = xQgBase >> m_sps->log2_min_luma_coding_block_size;
  const int y_cb = yQgBase >> m_sps->log2_min_luma_coding_block_size;
  const int availableA = (xBase & ctb_size_mask) && (xQgBase & ctb_size_mask);
  const int availableB = (yBase & ctb_size_mask) && (yQgBase & ctb_size_mask);

  int qpy_pred_local = header ? header->SliceQpY : 0;
  if (first_qp_group || (!xQgBase && !yQgBase)) {
    first_qp_group = !IsCuQpDeltaCoded;
  } else {
    qpy_pred_local = qPy_pred;
  }

  int qpy_a = qpy_pred_local;
  if (availableA && x_cb > 0) {
    const int idx = y_cb * m_sps->min_cb_width + (x_cb - 1);
    if (idx >= 0 && idx < (int)qp_y_tab.size()) qpy_a = qp_y_tab[idx];
  }

  int qpy_b = qpy_pred_local;
  if (availableB && y_cb > 0) {
    const int idx = (y_cb - 1) * m_sps->min_cb_width + x_cb;
    if (idx >= 0 && idx < (int)qp_y_tab.size()) qpy_b = qp_y_tab[idx];
  }

  return (qpy_a + qpy_b + 1) >> 1;
}

void SliceData::set_qPy(int xBase, int yBase, int log2CbSize) {
  if (!m_sps || !m_pps || !header) return;

  int qp_y_pred = get_qPy_pred(xBase, yBase, log2CbSize);
  if (CuQpDeltaVal != 0) {
    const int off = m_sps->qp_bd_offset;
    const int mod = 52 + off;
    int qp = qp_y_pred + CuQpDeltaVal + 52 + 2 * off;
    qp %= mod;
    if (qp < 0) qp += mod;
    qp_y = qp - off;
  } else {
    qp_y = qp_y_pred;
  }
  if (xBase < 384 && yBase < 320) {
    std::cout << "DBG ours qpy_call pos=(" << xBase << "," << yBase
              << ") log2=" << log2CbSize << " pred=" << qp_y_pred
              << " delta=" << CuQpDeltaVal << " out=" << qp_y
              << " first=" << first_qp_group
              << " coded=" << IsCuQpDeltaCoded << std::endl;
  }
}

void SliceData::fill_qp_y_tab(int x0, int y0, int log2CbSize) {
  if (!m_sps || qp_y_tab.empty()) return;

  const int length =
      (1 << log2CbSize) >> m_sps->log2_min_luma_coding_block_size;
  const int x_cb = x0 >> m_sps->log2_min_luma_coding_block_size;
  const int y_cb = y0 >> m_sps->log2_min_luma_coding_block_size;

  for (int y = 0; y < length; ++y) {
    const int row = y_cb + y;
    if (row < 0 || row >= m_sps->min_cb_height) continue;
    for (int x = 0; x < length; ++x) {
      const int col = x_cb + x;
      if (col < 0 || col >= m_sps->min_cb_width) continue;
      qp_y_tab[row * m_sps->min_cb_width + col] = (int8_t)qp_y;
    }
  }
}

/* 7.3.4 Slice data syntax */
int SliceData::slice_segment_data(BitStream &bitStream, PictureBase &picture,
                                  SPS &sps, PPS &pps) {
  /* 初始化类中的指针 */
  pic = &picture;
  header = pic->m_slice->slice_header;
  bs = &bitStream;
  m_sps = &sps;
  m_pps = &pps;
  m_pu = PU();
  //m_cu = CU();
  //m_tu = TU();

  if (cabac == nullptr) cabac = new Cabac(*bs, *pic);

  tab_ct_depth = new uint8_t[m_sps->min_cb_height * m_sps->min_cb_width]{0};
  tab_slice_address.assign(m_sps->PicSizeInCtbsY, -1);
  cu_skip_flag_ctx.assign(m_sps->min_cb_height * m_sps->min_cb_width, 0);
  qp_y_tab.assign(m_sps->min_cb_height * m_sps->min_cb_width,
                  (int8_t)header->SliceQpY);
  qp_y = header->SliceQpY;
  qPy_pred = header->SliceQpY;
  first_qp_group = !header->dependent_slice_segment_flag;
  reset_block_state_maps();
  const size_t ipm_size =
      (size_t)m_sps->min_pu_width * (size_t)m_sps->min_pu_height;
  if (header->first_slice_segment_in_pic_flag || tab_ipm.size() != ipm_size)
    tab_ipm.assign(ipm_size, 1);

  //----------------------- 开始对Slice分割为MacroBlock进行处理 ----------------------------

  const int first_ctb_addr_ts = m_pps->CtbAddrRsToTs[header->slice_ctb_addr_rs];
  const int first_ctb_row = header->slice_ctb_addr_rs / m_sps->PicWidthInCtbsY;
  const uint8_t *slice_data_start = bs->getP();
  const uint8_t *slice_data_end = bs->getEndBuf() + 1;
  std::vector<const uint8_t *> wpp_row_starts;
  const Nalu *slice_nalu = (pic && pic->m_slice) ? pic->m_slice->getNalu() : nullptr;

  // WPP 子流 entry points：row_starts[0] 为首行，后续每行按 raw payload
  // 偏移累加，再映射回去 EPB 后的 RBSP 边界。
  if (m_pps->entropy_coding_sync_enabled_flag && !m_pps->tiles_enabled_flag &&
      header->num_entry_point_offsets > 0 && slice_data_start < slice_data_end) {
    if (!build_wpp_row_starts_from_entry_points(slice_nalu, bs, slice_data_start,
                                                slice_data_end, header,
                                                wpp_row_starts)) {
      wpp_row_starts.clear();
      wpp_row_starts.reserve(header->num_entry_point_offsets + 1);
      wpp_row_starts.push_back(slice_data_start);

      const uint8_t *cursor = slice_data_start;
      for (int i = 0; i < header->num_entry_point_offsets; i++) {
        const int step = header->entry_point_offset_minus1[i] + 1;
        if (step <= 0) {
          std::cerr << "WARN: invalid entry_point_offset at " << i << std::endl;
          break;
        }
        if (cursor + step > slice_data_end) {
          std::cerr << "WARN: entry_point_offset out of range at " << i
                    << " (step=" << step
                    << ", remaining=" << (slice_data_end - cursor)
                    << ", num_entry_point_offsets="
                    << header->num_entry_point_offsets
                    << "), stop building WPP row starts." << std::endl;
          break;
        }
        cursor += step;
        wpp_row_starts.push_back(cursor);
      }
    }
  }
  if (m_pps->entropy_coding_sync_enabled_flag) {
    std::cout << "DBG ours wpp entry_points=" << header->num_entry_point_offsets
              << " row_starts=" << wpp_row_starts.size()
              << " first_ctb_row=" << first_ctb_row << std::endl;
  }

  CtbAddrInTs = first_ctb_addr_ts;
  bool wpp_saved_state_valid = false;
  uint8_t wpp_saved_cabac_state[HEVC_CONTEXTS] = {0};
  int wpp_saved_stat_coeff[4] = {0};
  bool end_of_slice_segment_flag = false;
  do {
    if (CtbAddrInTs < 0 || CtbAddrInTs >= m_sps->PicSizeInCtbsY) {
      std::cerr << "WARN: CtbAddrInTs out of range, stop current slice: "
                << CtbAddrInTs << std::endl;
      break;
    }

    // 编码顺序: 递增当前CTU的地址
    // 光栅顺序: 将编码顺序下的CTU地址转换为光栅顺序下的CTU地址
    CtbAddrInRs = m_pps->CtbAddrTsToRs[CtbAddrInTs];
    int32_t xCtb = (CtbAddrInRs % m_sps->PicWidthInCtbsY)
                   << m_sps->CtbLog2SizeY;
    int32_t yCtb = (CtbAddrInRs / m_sps->PicWidthInCtbsY)
                   << m_sps->CtbLog2SizeY;

    // CtbAddrInRs 是当前CTU在光栅顺序（Raster Scan）中的地址
    hls_decode_neighbour(xCtb, yCtb, CtbAddrInTs);

    // – 如果 CTU 是图块中的第一个 CTU，则以下规则适用：
    if (CtbAddrInTs == first_ctb_addr_ts) {
      //9.3.2.6 Initialization process for the arithmetic decoding engine
      if (!wpp_row_starts.empty()) {
        const uint8_t *row0_start = wpp_row_starts[0];
        if (cabac->ff_reinit_from(row0_start, slice_data_end) < 0) return -1;
      } else {
        if (cabac->ff_initialization_decoding_engine() < 0) return -1;
      }
      // 当前片段不是依赖片段,或者启用了 Tile 并且当前块与前一个块不在同一个 Tile 中，则初始化 CABAC 的状态
      if (header->dependent_slice_segment_flag == 0 ||
          (m_pps->tiles_enabled_flag &&
           m_pps->TileId[CtbAddrInTs] != m_pps->TileId[CtbAddrInTs - 1]))
        // – 上下文变量的初始化过程按照第 9.3.2.2 节的规定被调用。
        cabac->ff_initialization_context_variables(header);

      // – 变量 StatCoeff[ k ] 设置为等于 0，因为 k 的范围为 0 到 3（含）。
      for (int i = 0; i < 4; i++)
        StatCoeff[i] = 0;

      // – 按照第 9.3.2.3 节的规定调用调色板预测变量的初始化过程。 TODO 这里ffmpeg没有调用，为什么？ <24-12-14 09:06:21, YangJing>
      //cabac->initialization_palette_predictor_entries(m_sps, m_pps);
    } else {
      if (m_pps->tiles_enabled_flag &&
          m_pps->TileId[CtbAddrInTs] != m_pps->TileId[CtbAddrInTs - 1]) {
        // TODO: Tile 边界完整路径（含 entry_point）后续补齐；当前先做最小可用的重初始化。
        (void)cabac->ff_decode_terminate();
        if (cabac->ff_reinit_from_current_position() < 0) return -1;
        cabac->ff_initialization_context_variables(header);
        for (int i = 0; i < 4; i++)
          StatCoeff[i] = 0;
      }

      if (m_pps->entropy_coding_sync_enabled_flag &&
          (CtbAddrInTs % m_sps->ctb_width == 0)) {
        // WPP：在新行起始消费上一子流的终止符，并重建 CABAC 解码器。
        const int ctb_row = CtbAddrInRs / m_sps->PicWidthInCtbsY;
        const int wpp_row_idx = ctb_row - first_ctb_row;
        const uint8_t *wpp_row_start = nullptr;
        if (wpp_row_idx >= 0 && wpp_row_idx < (int)wpp_row_starts.size())
          wpp_row_start = wpp_row_starts[wpp_row_idx];
        std::cout << "DBG ours wpp_row ctb_row=" << ctb_row
                  << " idx=" << wpp_row_idx
                  << " has_start=" << (wpp_row_start != nullptr) << std::endl;

        (void)cabac->ff_decode_terminate();
        if (wpp_row_start) {
          if (cabac->ff_reinit_from(wpp_row_start, slice_data_end) < 0) return -1;
        } else {
          if (cabac->ff_reinit_from_current_position() < 0) return -1;
        }

        if (m_sps->ctb_width == 1 || !wpp_saved_state_valid) {
          cabac->ff_initialization_context_variables(header);
          for (int i = 0; i < 4; i++)
            StatCoeff[i] = 0;
        } else {
          cabac->ff_load_context_states(wpp_saved_cabac_state);
          if (m_sps->persistent_rice_adaptation_enabled_flag) {
            memcpy(StatCoeff, wpp_saved_stat_coeff, sizeof(wpp_saved_stat_coeff));
          }
        }
      }
    }

    // 解析当前CTU（编码树单元）的数据
    int more_data = coding_tree_unit();
    if (more_data < 0) return more_data;
    end_of_slice_segment_flag = (more_data == 0);

    if (!end_of_slice_segment_flag && m_pps->entropy_coding_sync_enabled_flag) {
      const int next_ctb_ts = CtbAddrInTs + 1;
      if ((next_ctb_ts % m_sps->ctb_width == 2) ||
          (m_sps->ctb_width == 2 && next_ctb_ts % m_sps->ctb_width == 0)) {
        cabac->ff_save_context_states(wpp_saved_cabac_state);
        if (m_sps->persistent_rice_adaptation_enabled_flag) {
          memcpy(wpp_saved_stat_coeff, StatCoeff, sizeof(wpp_saved_stat_coeff));
        }
        wpp_saved_state_valid = true;
      }
    }

    CtbAddrInTs++;
    if (CtbAddrInTs >= m_sps->PicSizeInCtbsY) {
      end_of_slice_segment_flag = true;
    }
  } while (!end_of_slice_segment_flag);
  return 0;
}

static const unsigned av_mod_uintp2_c(unsigned a, unsigned p) {
  return a & ((1U << p) - 1);
}

static inline int av_size_mult(size_t a, size_t b, size_t *r) {
  size_t t = a * b;
  /* Hack inspired from glibc: don't try the division if nelem and elsize
     * are both less than sqrt(SIZE_MAX). */
  if ((a | b) >= ((size_t)1 << (sizeof(size_t) * 4)) && a && t / a != b)
    return -1;
  *r = t;
  return 0;
}

#define BOUNDARY_LEFT_SLICE (1 << 0)
#define BOUNDARY_LEFT_TILE (1 << 1)
#define BOUNDARY_UPPER_SLICE (1 << 2)
#define BOUNDARY_UPPER_TILE (1 << 3)
void SliceData::hls_decode_neighbour(int x_ctb, int y_ctb, int ctb_addr_ts) {
  int ctb_size = 1 << m_sps->CtbLog2SizeY;
  int ctb_addr_rs = m_pps->CtbAddrTsToRs[ctb_addr_ts];
  int ctb_addr_in_slice = ctb_addr_rs - header->SliceAddrRs;
  if (ctb_addr_rs >= 0 && ctb_addr_rs < (int)tab_slice_address.size())
    tab_slice_address[ctb_addr_rs] = header->SliceAddrRs;

  if (m_pps->entropy_coding_sync_enabled_flag) {
    if (x_ctb == 0 && (y_ctb & (ctb_size - 1)) == 0) first_qp_group = 1;
    end_of_tiles_x = m_sps->width;
  } else if (m_pps->tiles_enabled_flag) {
    if (ctb_addr_ts &&
        m_pps->TileId[ctb_addr_ts] != m_pps->TileId[ctb_addr_ts - 1]) {
      int idxX = m_pps->col_idxX[x_ctb >> m_sps->CtbLog2SizeY];
      end_of_tiles_x = x_ctb + (m_pps->colWidth[idxX] << m_sps->CtbLog2SizeY);
      first_qp_group = 1;
    }
  } else {
    end_of_tiles_x = m_sps->width;
  }

  end_of_tiles_y = MIN(y_ctb + ctb_size, m_sps->height);

  int boundary_flags = 0;
  auto valid_ctb_rs = [&](int rs) {
    return rs >= 0 && rs < m_sps->PicSizeInCtbsY;
  };
  auto same_slice_addr = [&](int rs_a, int rs_b) {
    if (!valid_ctb_rs(rs_a) || !valid_ctb_rs(rs_b)) return false;
    if (rs_a >= (int)tab_slice_address.size() || rs_b >= (int)tab_slice_address.size())
      return false;
    return tab_slice_address[rs_a] == tab_slice_address[rs_b];
  };

  if (m_pps->tiles_enabled_flag) {
    if (x_ctb > 0 && valid_ctb_rs(ctb_addr_rs - 1) &&
        m_pps->TileId[ctb_addr_ts] !=
            m_pps->TileId[m_pps->CtbAddrRsToTs[ctb_addr_rs - 1]])
      boundary_flags |= BOUNDARY_LEFT_TILE;
    if (x_ctb > 0 && !same_slice_addr(ctb_addr_rs, ctb_addr_rs - 1))
      boundary_flags |= BOUNDARY_LEFT_SLICE;
    if (y_ctb > 0 && valid_ctb_rs(ctb_addr_rs - m_sps->ctb_width) &&
        m_pps->TileId[ctb_addr_ts] !=
            m_pps->TileId[m_pps->CtbAddrRsToTs[ctb_addr_rs - m_sps->ctb_width]])
      boundary_flags |= BOUNDARY_UPPER_TILE;
    if (y_ctb > 0 &&
        !same_slice_addr(ctb_addr_rs, ctb_addr_rs - m_sps->ctb_width))
      boundary_flags |= BOUNDARY_UPPER_SLICE;
  } else {
    if (ctb_addr_in_slice <= 0) boundary_flags |= BOUNDARY_LEFT_SLICE;
    if (ctb_addr_in_slice < m_sps->ctb_width)
      boundary_flags |= BOUNDARY_UPPER_SLICE;
  }

  ctb_left_flag = ((x_ctb > 0) && (ctb_addr_in_slice > 0) &&
                   !(boundary_flags & BOUNDARY_LEFT_TILE));
  ctb_up_flag = ((y_ctb > 0) && (ctb_addr_in_slice >= m_sps->ctb_width) &&
                 !(boundary_flags & BOUNDARY_UPPER_TILE));
  if (m_pps->tiles_enabled_flag) {
    ctb_up_right_flag =
        ((y_ctb > 0) && (ctb_addr_in_slice + 1 >= m_sps->ctb_width) &&
         valid_ctb_rs(ctb_addr_rs + 1 - m_sps->ctb_width) &&
         (m_pps->TileId[ctb_addr_ts] ==
          m_pps->TileId[m_pps->CtbAddrRsToTs[ctb_addr_rs + 1 -
                                             m_sps->ctb_width]]));
    ctb_up_left_flag = ((x_ctb > 0) && (y_ctb > 0) &&
                        (ctb_addr_in_slice - 1 >= m_sps->ctb_width) &&
                        valid_ctb_rs(ctb_addr_rs - 1 - m_sps->ctb_width) &&
                        (m_pps->TileId[ctb_addr_ts] ==
                         m_pps->TileId[m_pps->CtbAddrRsToTs[ctb_addr_rs - 1 -
                                                            m_sps->ctb_width]]));
  } else {
    ctb_up_right_flag =
        ((y_ctb > 0) && (ctb_addr_in_slice + 1 >= m_sps->ctb_width) &&
         (x_ctb + ctb_size < m_sps->width));
    ctb_up_left_flag = ((x_ctb > 0) && (y_ctb > 0) &&
                        (ctb_addr_in_slice - 1 >= m_sps->ctb_width));
  }
}

//6.4.1 Derivation process for z-scan order block availability
int SliceData::derivation_z_scan_order_block_availability(int xCurr, int yCurr,
                                                          int xNbY, int yNbY) {
  std::cout << "Into -> " << __FUNCTION__ << "():" << __LINE__ << std::endl;
  // TODO: 当前使用保守可用性返回，后续补齐标准推导过程。
  //int minBlockAddrCurr =
  //MinTbAddrZs[xCurr >> MinTbLog2SizeY][yCurr >> MinTbLog2SizeY];

  //int minBlockAddrN;
  //if (xNbY < 0 || yNbY < 0 || xNbY >= m_sps->pic_width_in_luma_samples ||
  //yNbY >= m_sps->pic_height_in_luma_samples) {
  //minBlockAddrN = -1;
  //} else {
  //minBlockAddrN = MinTbAddrZs[xNbY >> MinTbLog2SizeY][yNbY >> MinTbLog2SizeY];
  //}

  //bool availableN = true;
  //if (minBlockAddrN < 0 || minBlockAddrN > minBlockAddrCurr) {
  //availableN = false;
  //}

  //#define MIN_TB_ADDR_ZS(x, y)                                                   \
//  s->ps.pps->min_tb_addr_zs[(y) * (s->ps.sps->tb_mask + 2) + (x)]
  //
  //  int xCurr_ctb = xCurr >> s->ps.sps->CtbLog2SizeY;
  //  int yCurr_ctb = yCurr >> s->ps.sps->CtbLog2SizeY;
  //  int xN_ctb = xN >> s->ps.sps->CtbLog2SizeY;
  //  int yN_ctb = yN >> s->ps.sps->CtbLog2SizeY;
  //  if (yN_ctb < yCurr_ctb || xN_ctb < xCurr_ctb)
  //    return 1;
  //  else {
  //    int Curr = MIN_TB_ADDR_ZS(
  //        (xCurr >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask,
  //        (yCurr >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask);
  //    int N = MIN_TB_ADDR_ZS(
  //        (xN >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask,
  //        (yN >> s->ps.sps->log2_min_tb_size) & s->ps.sps->tb_mask);
  //    return N <= Curr;
  //  }

  return 0;
}

//6.5.2 Z-scan order array initialization process
int SliceData::Z_scan_order_array_initialization() {
  std::cout << "Into -> " << __FUNCTION__ << "():" << __LINE__ << std::endl;
  // TODO: 后续按标准完成 Z-scan 初始化细节。
  int MinTbAddrZs[32][32];
  //for (int y = 0; y < (m_sps->PicHeightInCtbsY
  //<< (m_sps->CtbLog2SizeY - m_sps->MinTbLog2SizeY));
  //y++)
  //for (int x = 0; x < (m_sps->PicWidthInCtbsY
  //<< (m_sps->CtbLog2SizeY - m_sps->MinTbLog2SizeY));
  //x++) {
  //int tbX = (x << m_sps->MinTbLog2SizeY) >> m_sps->CtbLog2SizeY;
  //int tbY = (y << m_sps->MinTbLog2SizeY) >> m_sps->CtbLog2SizeY;
  //int ctbAddrRs = PicWidthInCtbsY * tbY + tbX;
  //MinTbAddrZs[x][y] =
  //CtbAddrRsToTs[ctbAddrRs]
  //<< ((m_sps->CtbLog2SizeY - m_sps->MinTbLog2SizeY) * 2);
  //int p = 0;
  //for (int i = 0, p = 0; i < (CtbLog2SizeY − MinTbLog2SizeY); i++) {
  //int m = 1 << i;
  //p += (m & x ? m * m : 0) + (m & y ? 2 * m * m : 0);
  //}
  //MinTbAddrZs[x][y] += p;
  //}
  return 0;
}

int SliceData::coding_tree_unit() {
  // CtbAddrInRs 是当前CTU在光栅顺序（Raster Scan）中的地址
  // CTU在图像中的水平、垂直像素坐标
  int32_t xCtb = (CtbAddrInRs % m_sps->PicWidthInCtbsY) << m_sps->CtbLog2SizeY;
  int32_t yCtb = (CtbAddrInRs / m_sps->PicWidthInCtbsY) << m_sps->CtbLog2SizeY;

  // 样值自适应偏移（SAO，Sample Adaptive Offset）
  if (header->slice_sao_luma_flag || header->slice_sao_chroma_flag)
    // 将CTU的像素坐标转换为CTU的索引（即CTU在图像中的位置）
    sao(xCtb >> m_sps->CtbLog2SizeY, yCtb >> m_sps->CtbLog2SizeY);

  // 递归地处理当前CTU的四叉树（QuadTree）结构
  return coding_quadtree(xCtb, yCtb, m_sps->CtbLog2SizeY, 0);
}

int SliceData::sao(int32_t rx, int32_t ry) {
  std::cout << "Into -> " << __FUNCTION__ << "():" << __LINE__ << std::endl;
  int sao_merge_left_flag = 0;
  if ((header->slice_sao_luma_flag || header->slice_sao_chroma_flag) && rx > 0 &&
      ctb_left_flag) {
    sao_merge_left_flag = cabac->decode_bin(SAO_MERGE_FLAG);
    std::cout << "sao_merge_left_flag:" << sao_merge_left_flag << std::endl;
  }

  int sao_merge_up_flag = 0;
  if ((header->slice_sao_luma_flag || header->slice_sao_chroma_flag) && ry > 0 &&
      !sao_merge_left_flag && ctb_up_flag) {
    sao_merge_up_flag = cabac->decode_bin(elem_offset[SAO_MERGE_FLAG]); // ae(v);
    std::cout << "sao_merge_up_flag:" << sao_merge_up_flag << std::endl;
  }

  const int component_count = (m_sps->ChromaArrayType != 0 ? 3 : 1);
  int sao_type_idx[3] = {0, 0, 0};
  int sao_eo_class[3] = {0, 0, 0};
  int sao_offset_abs[3][4] = {{0}};
  int sao_offset_sign[3][4] = {{0}};
  int sao_band_position[3] = {0, 0, 0};

  for (int cIdx = 0; cIdx < component_count; ++cIdx) {
    const bool sao_enabled =
        (cIdx == 0) ? header->slice_sao_luma_flag : header->slice_sao_chroma_flag;
    if (!sao_enabled) continue;

    if (cIdx == 2) {
      sao_type_idx[2] = sao_type_idx[1];
      sao_eo_class[2] = sao_eo_class[1];
    } else if (!sao_merge_up_flag && !sao_merge_left_flag) {
      cabac->decode_sao_type_idx_luma(sao_type_idx[cIdx]);
    }

    if (sao_type_idx[cIdx] == 0) continue;

    for (int i = 0; i < 4; ++i)
      cabac->decode_sao_offset_abs(sao_offset_abs[cIdx][i]);

    if (sao_type_idx[cIdx] == 1) {
      for (int i = 0; i < 4; ++i)
        if (sao_offset_abs[cIdx][i] != 0)
          cabac->decode_sao_offset_sign(sao_offset_sign[cIdx][i]);
      cabac->decode_sao_band_position(sao_band_position[cIdx]);
    } else if (cIdx != 2) {
      cabac->decode_sao_eo_class(sao_eo_class[cIdx]);
    }
  }
  return 0;
}

void SliceData::set_ct_depth(SPS *sps, int x0, int y0, int log2_cb_size,
                             int ct_depth) {
  int length = (1 << log2_cb_size) >> sps->log2_min_luma_coding_block_size;
  int x_cb = x0 >> sps->log2_min_luma_coding_block_size;
  int y_cb = y0 >> sps->log2_min_luma_coding_block_size;

  for (int y = 0; y < length; y++)
    memset(&tab_ct_depth[(y_cb + y) * sps->min_cb_width + x_cb], ct_depth,
           length);
}

int SliceData::coding_quadtree(int x0, int y0, int log2CbSize, int cqtDepth) {
  /* 图像宽高参数 */
  int32_t pic_width_in_luma_samples = m_sps->pic_width_in_luma_samples;
  int32_t pic_height_in_luma_samples = m_sps->pic_height_in_luma_samples;
  /* 最小编码块尺寸 */
  int32_t MinCbLog2SizeY = m_sps->MinCbLog2SizeY;

  /* 当前四叉树深度 */
  this->ct_depth = cqtDepth;
  int split_cu_flag = 0;

  int Log2MinCuChromaQpOffsetSize =
      m_sps->CtbLog2SizeY - m_pps->diff_cu_chroma_qp_offset_depth;

  /* 当前块完全位于图像边界内,当前块尺寸大于最小编码块尺寸 */
  if (x0 + (1 << log2CbSize) <= pic_width_in_luma_samples &&
      y0 + (1 << log2CbSize) <= pic_height_in_luma_samples &&
      log2CbSize > MinCbLog2SizeY) {
    /* 解码split_cu_flag */
    cabac->decode_split_cu_flag(split_cu_flag, *m_sps, tab_ct_depth,
                                ctb_left_flag, ctb_up_flag, ct_depth, x0, y0);
    std::cout << "[if]split_cu_flag:" << split_cu_flag
              << ",cqtDepth:" << cqtDepth << std::endl;
    if (cqtDepth == 0 && log2CbSize == m_sps->CtbLog2SizeY &&
        vvc_force_root_split_enabled())
      split_cu_flag = 1;
  } else {
    /* 当块尺寸大于最小允许尺寸时强制分割 */
    split_cu_flag = (log2CbSize > m_sps->log2_min_luma_coding_block_size);
    std::cout << "[else]split_cu_flag:" << split_cu_flag
              << ",cqtDepth:" << cqtDepth << std::endl;
  }

  /* 当CU尺寸≥最小QP偏移尺寸时重置QP偏移状态 */
  if (m_pps->cu_qp_delta_enabled_flag &&
      log2CbSize >= (m_sps->CtbLog2SizeY - m_pps->diff_cu_qp_delta_depth)) {
    IsCuQpDeltaCoded = 0, CuQpDeltaVal = 0;
    printf("reset\n");
  }

  printf("IsCuQpDeltaCoded:%d,CuQpDeltaVal:%d,log2CbSize:%d\n",
         IsCuQpDeltaCoded, CuQpDeltaVal, log2CbSize);

  /* 当CU尺寸≥色度QP偏移最小尺寸时重置色度QP偏移状态 */
  if (header->cu_chroma_qp_offset_enabled_flag &&
      log2CbSize >= Log2MinCuChromaQpOffsetSize)
    IsCuChromaQpOffsetCoded = 0;

  /* 四叉树递归处理:需要分割 */
  if (split_cu_flag) {
    int more_data = 0;
    /* 计算子块坐标（x1 = x0 + 半宽，y1 = y0 + 半高） */
    int x1 = x0 + (1 << (log2CbSize - 1));
    int y1 = y0 + (1 << (log2CbSize - 1));

    /* 递归处理四个子块：每次递归将块尺寸减半，深度+1 */
    /* 左上子块（x0, y0）*/
    more_data = coding_quadtree(x0, y0, log2CbSize - 1, cqtDepth + 1);
    if (more_data < 0) return more_data;
    /* 右上子块（x1, y0）（仅当x1不越界）*/
    if (more_data && x1 < pic_width_in_luma_samples) {
      more_data = coding_quadtree(x1, y0, log2CbSize - 1, cqtDepth + 1);
      if (more_data < 0) return more_data;
    }
    /* 左下子块（x0, y1）（仅当y1不越界）*/
    if (more_data && y1 < pic_height_in_luma_samples) {
      more_data = coding_quadtree(x0, y1, log2CbSize - 1, cqtDepth + 1);
      if (more_data < 0) return more_data;
    }
    /* 右下子块（x1, y1）（仅当x1和y1都不越界）*/
    if (more_data && x1 < pic_width_in_luma_samples &&
        y1 < pic_height_in_luma_samples) {
      more_data = coding_quadtree(x1, y1, log2CbSize - 1, cqtDepth + 1);
      if (more_data < 0) return more_data;
    }
    return more_data;
  }
  /* 叶子节点处理: 当不需要分割时，执行预测/变换/量化等核心编码操作 */
  else {
    const int cbSize = 1 << log2CbSize;
    const int ctbSize = 1 << m_sps->CtbLog2SizeY;
    coding_unit(x0, y0, log2CbSize);

    const bool end_x =
        (((x0 + cbSize) % ctbSize) == 0 || (x0 + cbSize >= pic_width_in_luma_samples));
    const bool end_y =
        (((y0 + cbSize) % ctbSize) == 0 || (y0 + cbSize >= pic_height_in_luma_samples));
    if (end_x && end_y) {
      int end_of_slice_flag = cabac->ff_decode_terminate(); // ae(v)
      return !end_of_slice_flag;
    }
    return 1;
  }
}

int SliceData::coding_unit(int x0, int y0, int log2CbSize) {
  /* 是否跳过变换和量化过程 */
  cu_transquant_bypass_flag = false;
  m_pu = PU();
  for (int idx = 0; idx < 4; ++idx) {
    m_pu.intra_pred_mode[idx] = DC_IDX;
    m_pu.intra_pred_mode_c[idx] = DC_IDX;
    m_pu.intra_chroma_pred_mode[idx] = 0;
  }
  const int ux = state_x(x0);
  const int uy = state_y(y0);
  const int nCbS = 1 << log2CbSize;
  int palette_mode_flag = 0;
  /* 最大亮度变换块大小的对数 */
  MaxTbLog2SizeY = m_sps->log2_min_luma_transform_block_size +
                   m_sps->log2_diff_max_min_luma_transform_block_size;
  /* 初始化划分模式 (part_mode) 为 PART_2Nx2N，表示不划分。HEVC 中，一个 CU 可以被划分为 1 个、2 个或 4 个预测单元（Prediction Unit, PU）*/
  int part_mode = PART_2Nx2N;
  int &PartMode = part_mode;
  CurrPartMode = PART_2Nx2N;
  /*  初始化帧内分割标志 (IntraSplitFlag) 为 0，表示不进行帧内分割 */
  IntraSplitFlag = 0;

  /* 当前 CU 在 CTB（Coding Tree Block）网格中的水平，垂直坐标 x_cb,y_cb */
  int x_cb = x0 >> m_sps->log2_min_luma_coding_block_size;
  int y_cb = y0 >> m_sps->log2_min_luma_coding_block_size;

  /* 将当前 CU 的预测模式设置为帧内预测 */
  fill_block_map(CuPredMode, x0, y0, nCbS, nCbS, MODE_INTRA);
  /* 是否跳过当前 CU 的编码 */
  fill_block_map(cu_skip_flag, x0, y0, nCbS, nCbS, 0);

  if (m_pps->transquant_bypass_enabled_flag) {
    cu_transquant_bypass_flag = cabac->decode_bin(IHEVC_CAB_CU_TQ_BYPASS_FLAG);
    std::cout << "cu_transquant_bypass_flag:" << cu_transquant_bypass_flag
              << std::endl;
  }

  /*  如果当前 slice 不是 I slice */
  if (header->slice_type != HEVC_SLICE_I) {
    const uint8_t cu_skip =
        cabac->decode_cu_skip_flag(x0, y0, x_cb, y_cb, ctb_left_flag,
                                   ctb_up_flag, cu_skip_flag_ctx.data(),
                                   m_sps->min_cb_width); //ae(v);
    fill_block_map(cu_skip_flag, x0, y0, nCbS, nCbS, cu_skip);
    if (x_cb >= 0 && y_cb >= 0 && x_cb < m_sps->min_cb_width &&
        y_cb < m_sps->min_cb_height) {
      cu_skip_flag_ctx[y_cb * m_sps->min_cb_width + x_cb] = cu_skip_flag[ux][uy];
    }
    std::cout << "cu_skip_flag:" << (int)cu_skip_flag[ux][uy] << std::endl;
    fill_block_map(CuPredMode, x0, y0, nCbS, nCbS,
                   cu_skip_flag[ux][uy] ? MODE_SKIP : MODE_INTER);
  }

  /* 如果当前 CU 是跳过模式 */
  if (cu_skip_flag[ux][uy])
    /* 调用 prediction_unit 函数处理预测单元。对于跳过模式，CU 直接作为 PU 处理，不进行进一步划分。 */
    prediction_unit(x0, y0, nCbS, nCbS);
  else {
    if (header->slice_type != HEVC_SLICE_I) {
      int pred_mode_flag = cabac->decode_bin(IHEVC_CAB_PRED_MODE); // ae(v)
      std::cout << "pred_mode_flag:" << pred_mode_flag << std::endl;
      fill_block_map(CuPredMode, x0, y0, nCbS, nCbS,
                     pred_mode_flag ? MODE_INTRA : MODE_INTER);
    }
    /* 如果启用了调色板模式，当前 CU 使用帧内预测，并且 CU 大小小于等于最大变换块大小。 */
    if (m_sps->palette_mode_enabled_flag && CuPredMode[ux][uy] == MODE_INTRA &&
        log2CbSize <= MaxTbLog2SizeY) {
      palette_mode_flag = 0; //ae(v);
      std::cout << "palette_mode_flag:" << palette_mode_flag << std::endl;
    }
    /* 当前 CU 使用调色板模式 */
    if (palette_mode_flag)
      palette_coding(x0, y0, nCbS);
    else {
      /* 当前 CU 不使用调色板模式 */

      int pcm_flag = 0;
      /* 如果当前 CU 不是帧内预测，或者 CU 大小等于最小 CU 大小 */
      if (CuPredMode[ux][uy] != MODE_INTRA ||
          log2CbSize == m_sps->MinCbLog2SizeY) {
        /* 解码划分模式 part_mode */
        part_mode = cabac->ff_hevc_part_mode_decode(
            log2CbSize, CuPredMode[ux][uy]); //ae(v);
        CurrPartMode = part_mode;
        std::cout << "part_mode:" << part_mode << std::endl;
        /* 如果划分模式为 PART_NxN 并且当前 CU 使用帧内预测，则设置 IntraSplitFlag 为 1 */
        IntraSplitFlag = part_mode == PART_NxN && CuPredMode[ux][uy] == MODE_INTRA;
      }

      /* 处理帧内预测 */
      if (CuPredMode[ux][uy] == MODE_INTRA) {
        /* 如果不划分 CU，启用了 PCM 模式，并且 CU 大小在 PCM 模式允许的范围内 */
        if (PartMode == PART_2Nx2N && m_sps->pcm_enabled_flag &&
            log2CbSize >= m_sps->log2_min_pcm_luma_coding_block_size &&
            log2CbSize <= m_sps->log2_max_pcm_luma_coding_block_size) {
          pcm_flag = 0; //ae(v);
          std::cout << "pcm_flag:" << pcm_flag << std::endl;
        }

        /* 如果当前 CU 使用 PCM 模式 */
        if (pcm_flag) {
          while (!bs->byte_aligned()) (void)bs->readU1(); // pcm_alignment_zero_bit
          pcm_sample(x0, y0, log2CbSize);
        }
        /* 如果当前 CU 不使用 PCM 模式 */
        else {
          // NOTE: 帧内预测 ffmepg -> intra_prediction_unit(s, x0, y0, log2CbSize);
          int i, j;
          int prev_intra_luma_pred_flag[4][4] = {{0}};
          int mpm_idx[4][4] = {{0}};
          int split = part_mode == PART_NxN;
          int pb_size = (1 << log2CbSize) >> split;
          int pbOffset = (PartMode == PART_NxN) ? (nCbS / 2) : nCbS;
          /* 循环遍历每个 PU（根据 PartMode 确定 PU 的大小和位置） */
          for (j = 0; j < nCbS; j = j + pbOffset)
            for (i = 0; i < nCbS; i = i + pbOffset) {
              const int pj = j / pbOffset;
              const int pi = i / pbOffset;
              /* prev_intra_luma_pred_flag指示是否使用最可能模式（Most Probable Mode, MPM）列表中的模式 */
              prev_intra_luma_pred_flag[pi][pj] = cabac->decode_bin(
                  elem_offset[PREV_INTRA_LUMA_PRED_FLAG]); // ae(v);
              std::cout << "prev_intra_luma_pred_flag:"
                        << prev_intra_luma_pred_flag[pi][pj]
                        << std::endl;
              fill_block_map(rem_intra_luma_pred_mode, x0 + i, y0 + j, pb_size,
                             pb_size, 0);
              fill_block_map(intra_chroma_pred_mode, x0 + i, y0 + j, pb_size,
                             pb_size, 0);
            }
          for (j = 0; j < nCbS; j = j + pbOffset)
            for (i = 0; i < nCbS; i = i + pbOffset) {
              const int pj = j / pbOffset;
              const int pi = i / pbOffset;
              const int pu_idx = 2 * pj + pi;
              const int uxi = state_x(x0 + i);
              const int uyi = state_y(y0 + j);
              if (prev_intra_luma_pred_flag[pi][pj]) {
                /* mpm_idx 指示使用 MPM 列表中的哪个模式 */
                mpm_idx[pi][pj] = cabac->ff_hevc_mpm_idx_decode(); // ae(v);
                m_pu.mpm_idx = mpm_idx[pi][pj];
                std::cout << "mpm_idx:" << mpm_idx[pi][pj] << std::endl;
              } else {
                /* em_intra_luma_pred_mode 指示在不使用 MPM 列表时的模式 */
                const uint8_t rem_intra_luma_pred_mode_val =
                    cabac->ff_hevc_rem_intra_luma_pred_mode_decode(); // ae(v);
                fill_block_map(rem_intra_luma_pred_mode, x0 + i, y0 + j,
                               pb_size, pb_size, rem_intra_luma_pred_mode_val);
                m_pu.rem_intra_luma_pred_mode =
                    rem_intra_luma_pred_mode[uxi][uyi];
                printf("rem_intra_luma_pred_mode:%d\n",
                       rem_intra_luma_pred_mode[uxi][uyi]);
              }
              m_pu.intra_pred_mode[pu_idx] = luma_intra_pred_mode(
                  x0 + i, y0 + j, pb_size, prev_intra_luma_pred_flag[pi][pj]);
            }
          if (m_sps->ChromaArrayType == 3) {
            for (j = 0; j < nCbS; j = j + pbOffset)
              for (i = 0; i < nCbS; i = i + pbOffset) {
                const int pu_idx = 2 * (j / pbOffset) + (i / pbOffset);
                const int uxi = state_x(x0 + i);
                const int uyi = state_y(y0 + j);
                /* 解码色度帧内预测模式 */
                const uint8_t chroma_mode =
                    cabac->ff_hevc_intra_chroma_pred_mode_decode(); //ae(v);
                m_pu.intra_chroma_pred_mode[pu_idx] = chroma_mode;
                m_pu.intra_pred_mode_c[pu_idx] = hevc_derive_intra_chroma_pred_mode(
                    m_pu.intra_pred_mode[pu_idx], chroma_mode);
                fill_block_map(intra_chroma_pred_mode, x0 + i, y0 + j, pb_size,
                               pb_size, chroma_mode);
                std::cout << "intra_chroma_pred_mode:"
                          << intra_chroma_pred_mode[uxi][uyi]
                          << std::endl;
              }
          } else if (m_sps->ChromaArrayType != 0) {
            const int ucx = state_x(x0);
            const int ucy = state_y(y0);
            /* 解码色度帧内预测模式 */
            const uint8_t chroma_mode =
                cabac->ff_hevc_intra_chroma_pred_mode_decode(); //ae(v);
            m_pu.intra_chroma_pred_mode[0] = chroma_mode;
            if (m_sps->ChromaArrayType == 2) {
              const int mode_idx =
                  hevc_derive_intra_chroma_pred_mode(m_pu.intra_pred_mode[0],
                                                     chroma_mode);
              m_pu.intra_pred_mode_c[0] = hevc_tab_mode_idx(mode_idx);
            } else {
              m_pu.intra_pred_mode_c[0] = hevc_derive_intra_chroma_pred_mode(
                  m_pu.intra_pred_mode[0], chroma_mode);
            }
            fill_block_map(intra_chroma_pred_mode, x0, y0, nCbS, nCbS,
                           chroma_mode);
            printf("chroma_mode:%d\n", intra_chroma_pred_mode[ucx][ucy]);
            if (m_sps->ChromaArrayType == 2) {
              std::cout << "intra_chroma_pred_mode:"
                        << intra_chroma_pred_mode[ucx][ucy] << std::endl;
            }
          }
        }
      }
      /* 处理帧间预测 (CuPredMode[x0][y0] != MODE_INTRA) */
      else {
        /* 根据 PartMode 的值，将 CU 划分为一个或多个 PU，并对每个 PU 调用 prediction_unit 函数 */
        if (PartMode == PART_2Nx2N)
          prediction_unit(x0, y0, nCbS, nCbS);
        else if (PartMode == PART_2NxN) {
          prediction_unit(x0, y0, nCbS, nCbS / 2);
          prediction_unit(x0, y0 + (nCbS / 2), nCbS, nCbS / 2);
        } else if (PartMode == PART_Nx2N) {
          prediction_unit(x0, y0, nCbS / 2, nCbS);
          prediction_unit(x0 + (nCbS / 2), y0, nCbS / 2, nCbS);
        } else if (PartMode == PART_2NxnU) {
          prediction_unit(x0, y0, nCbS, nCbS / 4);
          prediction_unit(x0, y0 + (nCbS / 4), nCbS, nCbS * 3 / 4);
        } else if (PartMode == PART_2NxnD) {
          prediction_unit(x0, y0, nCbS, nCbS * 3 / 4);
          prediction_unit(x0, y0 + (nCbS * 3 / 4), nCbS, nCbS / 4);
        } else if (PartMode == PART_nLx2N) {
          prediction_unit(x0, y0, nCbS / 4, nCbS);
          prediction_unit(x0 + (nCbS / 4), y0, nCbS * 3 / 4, nCbS);
        } else if (PartMode == PART_nRx2N) {
          prediction_unit(x0, y0, nCbS * 3 / 4, nCbS);
          prediction_unit(x0 + (nCbS * 3 / 4), y0, nCbS / 4, nCbS);
        } else { /* PART_NxN */
          prediction_unit(x0, y0, nCbS / 2, nCbS / 2);
          prediction_unit(x0 + (nCbS / 2), y0, nCbS / 2, nCbS / 2);
          prediction_unit(x0, y0 + (nCbS / 2), nCbS / 2, nCbS / 2);
          prediction_unit(x0 + (nCbS / 2), y0 + (nCbS / 2), nCbS / 2, nCbS / 2);
        }
      }
      /* 如果当前 CU 不使用 PCM 模式 */
      if (!pcm_flag) {
        /* 表示残差四叉树（Residual Quadtree, RQT）的根节点是否存在编码块标志（Coded Block Flag, CBF） */
        int rqt_root_cbf = 1;
        /* 如果当前 CU 不是帧内预测，并且不是不划分的merge模式 */
        if (CuPredMode[ux][uy] != MODE_INTRA &&
            !(PartMode == PART_2Nx2N && merge_flag[ux][uy])) {
          rqt_root_cbf = cabac->ff_hevc_no_residual_syntax_flag_decode(); //ae(v);
          std::cout << "rqt_root_cbf:" << rqt_root_cbf << std::endl;
        }
        if (rqt_root_cbf) {
          /* 计算最大变换深度 */
          MaxTrafoDepth = (CuPredMode[ux][uy] == MODE_INTRA
                               ? (m_sps->max_transform_hierarchy_depth_intra +
                                  IntraSplitFlag)
                               : m_sps->max_transform_hierarchy_depth_inter);
          /*  调用 transform_tree 函数处理变换树 */
          static const int zero_cbf[2] = {0, 0};
          transform_tree(x0, y0, x0, y0, x0, y0, log2CbSize, log2CbSize, 0, 0,
                         zero_cbf, zero_cbf);
        }
      }
    }
  }

  if (m_pps->cu_qp_delta_enabled_flag) {
    if (!IsCuQpDeltaCoded) set_qPy(x0, y0, log2CbSize);
  } else {
    qp_y = header->SliceQpY;
  }
  fill_qp_y_tab(x0, y0, log2CbSize);

  const int qp_block_mask =
      (1 << (m_sps->CtbLog2SizeY - m_pps->diff_cu_qp_delta_depth)) - 1;
  if (((x0 + (1 << log2CbSize)) & qp_block_mask) == 0 &&
      ((y0 + (1 << log2CbSize)) & qp_block_mask) == 0) {
    qPy_pred = qp_y;
  }

  /* 设置编码树（Coding Tree, CT）的深度 */
  set_ct_depth(m_sps, x0, y0, log2CbSize, ct_depth);
  return 0;
}

int SliceData::reconstruct_intra_luma_block(int x0, int y0, int nCbS,
                                            int intra_mode) {
  if (!pic || !pic->m_pic_buff_luma || !m_sps || nCbS <= 0) return 0;
  intra_mode = Clip3(PLANAR_IDX, 34, intra_mode);

  const int width = pic->PicWidthInSamplesL;
  const int height = pic->PicHeightInSamplesL;
  if (width <= 0 || height <= 0) return 0;

  if (x0 >= width || y0 >= height || x0 < 0 || y0 < 0) return 0;

  const int x1 = MIN(x0 + nCbS, width);
  const int y1 = MIN(y0 + nCbS, height);
  if (x1 <= x0 || y1 <= y0) return 0;

  uint8_t *dst = pic->m_pic_buff_luma;
  const int size = nCbS;
  const int block_w = x1 - x0;
  const int block_h = y1 - y0;
  const int bit_depth = m_sps ? m_sps->BitDepthY : 8;
  const int default_sample = 1 << (bit_depth - 1);
  int log2_size = 0;
  while ((1 << log2_size) < size) ++log2_size;
  const int ctb_size = 1 << m_sps->CtbLog2SizeY;
  const int x0b = av_mod_uintp2_c(x0, m_sps->CtbLog2SizeY);
  const int y0b = av_mod_uintp2_c(y0, m_sps->CtbLog2SizeY);
  const int min_tb_log2 = m_sps->log2_min_luma_transform_block_size;
  const int min_tb_size = 1 << min_tb_log2;
  const int x_tb = (x0 >> min_tb_log2) & m_sps->tb_mask;
  const int y_tb = (y0 >> min_tb_log2) & m_sps->tb_mask;
  const int size_in_tbs = size >> min_tb_log2;
  const int curr_tb_addr = hevc_min_tb_addr_zs(m_pps, m_sps, x_tb, y_tb);

  std::vector<int> top_storage(2 * size + 1, default_sample);
  std::vector<int> left_storage(2 * size + 1, default_sample);
  std::vector<int> filtered_top_storage(2 * size + 1, default_sample);
  std::vector<int> filtered_left_storage(2 * size + 1, default_sample);
  int *top = top_storage.data() + 1;
  int *left = left_storage.data() + 1;
  int *filtered_top = filtered_top_storage.data() + 1;
  int *filtered_left = filtered_left_storage.data() + 1;

  bool cand_up = ctb_up_flag || y0b;
  bool cand_left = ctb_left_flag || x0b;
  bool cand_up_left = (x0b || y0b) ? (cand_left && cand_up) : ctb_up_left_flag;
  bool cand_up_right_sap =
      (x0b + size == ctb_size) ? (ctb_up_right_flag && !y0b) : cand_up;
  bool cand_up_right = cand_up_right_sap && (x0 + size) < end_of_tiles_x;
  bool cand_bottom_left = ((y0 + size) >= end_of_tiles_y) ? false : cand_left;

  if (cand_up_right && y0b > 0) {
    const int up_right_tb_addr =
        hevc_min_tb_addr_zs(m_pps, m_sps,
                            (x_tb + size_in_tbs) & m_sps->tb_mask,
                            y_tb - 1);
    cand_up_right = curr_tb_addr > up_right_tb_addr;
  }
  if (cand_bottom_left && x0b > 0) {
    const int bottom_left_tb_addr =
        hevc_min_tb_addr_zs(m_pps, m_sps, x_tb - 1,
                            (y_tb + size_in_tbs) & m_sps->tb_mask);
    cand_bottom_left = curr_tb_addr > bottom_left_tb_addr;
  }

  bool has_top = cand_up && y0 > 0;
  bool has_left = cand_left && x0 > 0;
  bool has_top_left = cand_up_left && x0 > 0 && y0 > 0;

  int top_samples = 0;
  int left_samples = 0;
  int top_right_size = 0;
  int bottom_left_size = 0;

  if (has_top_left) {
    top[-1] = dst[(y0 - 1) * width + (x0 - 1)];
    left[-1] = top[-1];
  } else {
    top[-1] = default_sample;
    left[-1] = default_sample;
  }

  if (has_top) {
    top_samples = MIN(size, width - x0);
    if (top_samples > 0) {
      const int top_row = (y0 - 1) * width;
      for (int x = 0; x < top_samples; ++x) top[x] = dst[top_row + x0 + x];
      if (top_samples < size) {
        std::fill_n(top + top_samples, size - top_samples, top[top_samples - 1]);
      }
    } else {
      has_top = false;
    }
  }

  if (has_left) {
    left_samples = MIN(size, height - y0);
    if (left_samples > 0) {
      for (int y = 0; y < left_samples; ++y)
        left[y] = dst[(y0 + y) * width + (x0 - 1)];
      if (left_samples < size) {
        std::fill_n(left + left_samples, size - left_samples,
                    left[left_samples - 1]);
      }
    } else {
      has_left = false;
    }
  }

  if (has_top && cand_up_right) {
    top_right_size = MIN(size, MAX(0, width - (x0 + size)));
    if (top_right_size > 0) {
      const int top_row = (y0 - 1) * width;
      for (int x = 0; x < top_right_size; ++x)
        top[size + x] = dst[top_row + x0 + size + x];
      if (top_right_size < size) {
        std::fill_n(top + size + top_right_size, size - top_right_size,
                    top[size + top_right_size - 1]);
      }
    }
  }

  if (has_left && cand_bottom_left) {
    bottom_left_size = MIN(size, MAX(0, height - (y0 + size)));
    if (bottom_left_size > 0) {
      for (int y = 0; y < bottom_left_size; ++y)
        left[size + y] = dst[(y0 + size + y) * width + (x0 - 1)];
      if (bottom_left_size < size) {
        std::fill_n(left + size + bottom_left_size, size - bottom_left_size,
                    left[size + bottom_left_size - 1]);
      }
    }
  }

  if (!bottom_left_size) {
    if (has_left) {
      std::fill_n(left + size, size, left[size - 1]);
    } else if (has_top_left) {
      std::fill_n(left, 2 * size, left[-1]);
      has_left = true;
    } else if (has_top) {
      left[-1] = top[0];
      std::fill_n(left, 2 * size, left[-1]);
      has_top_left = true;
      has_left = true;
    } else {
      left[-1] = default_sample;
      top[-1] = default_sample;
      std::fill_n(top, 2 * size, default_sample);
      std::fill_n(left, 2 * size, default_sample);
      has_top = true;
      has_top_left = true;
      has_left = true;
    }
  }

  if (!has_left) std::fill_n(left, size, left[size]);
  if (!has_top_left) left[-1] = left[0];
  if (!has_top) std::fill_n(top, size, left[-1]);
  if (!top_right_size) std::fill_n(top + size, size, top[size - 1]);
  top[-1] = left[-1];

  int *pred_top = top;
  int *pred_left = left;
  if (size > 4 && intra_mode != DC_IDX && log2_size >= 3 && log2_size <= 5) {
    static const int intra_hor_ver_dist_thresh[] = {7, 1, 0};
    const int min_dist_vert_hor =
        MIN(std::abs(intra_mode - 26), std::abs(intra_mode - 10));
    if (min_dist_vert_hor >
        intra_hor_ver_dist_thresh[log2_size - 3]) {
      const int threshold = 1 << (bit_depth - 5);
      if (m_sps && m_sps->strong_intra_smoothing_enabled_flag &&
          log2_size == 5 &&
          std::abs(top[-1] + top[2 * size - 1] - 2 * top[size - 1]) <
              threshold &&
          std::abs(left[-1] + left[2 * size - 1] - 2 * left[size - 1]) <
              threshold) {
        filtered_top[-1] = top[-1];
        filtered_left[-1] = left[-1];
        filtered_top[2 * size - 1] = top[2 * size - 1];
        filtered_left[2 * size - 1] = left[2 * size - 1];
        for (int i = 0; i < 2 * size - 1; ++i) {
          filtered_top[i] =
              ((2 * size - (i + 1)) * top[-1] +
               (i + 1) * top[2 * size - 1] + size) >>
              (log2_size + 1);
          filtered_left[i] =
              ((2 * size - (i + 1)) * left[-1] +
               (i + 1) * left[2 * size - 1] + size) >>
              (log2_size + 1);
        }
      } else {
        filtered_left[2 * size - 1] = left[2 * size - 1];
        filtered_top[2 * size - 1] = top[2 * size - 1];
        for (int i = 2 * size - 2; i >= 0; --i)
          filtered_left[i] =
              (left[i + 1] + 2 * left[i] + left[i - 1] + 2) >> 2;
        filtered_top[-1] = filtered_left[-1] =
            (left[0] + 2 * left[-1] + top[0] + 2) >> 2;
        for (int i = 2 * size - 2; i >= 0; --i)
          filtered_top[i] =
              (top[i + 1] + 2 * top[i] + top[i - 1] + 2) >> 2;
      }
      pred_top = filtered_top;
      pred_left = filtered_left;
    }
  }

  auto store_pred = [&](int bx, int by, int value) {
    dst[(y0 + by) * width + x0 + bx] =
        static_cast<uint8_t>(ClipBD(value, bit_depth));
  };
  const bool dbg_focus =
      (x0 < 16 && y0 < 16) ||
      (x0 >= 224 && x0 <= 544 && y0 >= 192 && y0 <= 544) ||
      (x0 >= 256 && y0 >= 448);
  auto log_pred_block = [&]() {
    if (dbg_focus) {
      std::cout << "DBG ours pred_meta pos=(" << x0 << "," << y0
                << ") size=" << size << " mode=" << intra_mode
                << " candBL=" << cand_bottom_left
                << " blSize=" << bottom_left_size
                << " candUR=" << cand_up_right
                << " topRef=";
      for (int i = -1; i <= size; ++i) {
        if (i != -1) std::cout << ",";
        std::cout << pred_top[i];
      }
      std::cout << " leftRef=";
      for (int i = -1; i <= size; ++i) {
        if (i != -1) std::cout << ",";
        std::cout << pred_left[i];
      }
      std::cout << std::endl;
    }
    std::cout << "DBG ours pred_block pos=(" << x0 << "," << y0
              << ") size=" << block_w << " mode=" << intra_mode;
    if (dbg_focus) {
      std::cout << " candBL=" << cand_bottom_left
                << " blSize=" << bottom_left_size
                << " candUR=" << cand_up_right
                << " leftLast=" << left[size - 1]
                << " leftNext=" << left[size];
    }
    std::cout << std::endl;
    if (!dbg_focus) return;
    for (int row = 0; row < block_h; ++row) {
      std::cout << "DBG ours pred_row pos=(" << x0 << "," << y0
                << ") row=" << row << " vals=";
      for (int col = 0; col < block_w; ++col) {
        if (col) std::cout << ",";
        std::cout << (int)dst[(y0 + row) * width + x0 + col];
      }
      std::cout << std::endl;
    }
  };

  if (intra_mode == PLANAR_IDX) {
    for (int y = 0; y < block_h; ++y)
      for (int x = 0; x < block_w; ++x)
        store_pred(
            x, y,
            ((size - 1 - x) * pred_left[y] + (x + 1) * pred_top[size] +
             (size - 1 - y) * pred_top[x] + (y + 1) * pred_left[size] + size) >>
                (log2_size + 1));
    log_pred_block();
    return 0;
  }

  if (intra_mode == DC_IDX) {
    int dc = size;
    for (int i = 0; i < size; ++i) dc += pred_left[i] + pred_top[i];
    dc >>= (log2_size + 1);

    for (int y = 0; y < block_h; ++y)
      for (int x = 0; x < block_w; ++x) store_pred(x, y, dc);

    if (size < 32 && block_w > 0 && block_h > 0) {
      store_pred(0, 0, (pred_left[0] + 2 * dc + pred_top[0] + 2) >> 2);
      for (int x = 1; x < block_w; ++x)
        store_pred(x, 0, (pred_top[x] + 3 * dc + 2) >> 2);
      for (int y = 1; y < block_h; ++y)
        store_pred(0, y, (pred_left[y] + 3 * dc + 2) >> 2);
    }
    log_pred_block();
    return 0;
  }

  static const int intra_pred_angle[] = {
      32,  26,  21,  17, 13,  9,  5, 2, 0, -2, -5,
      -9, -13, -17, -21, -26, -32, -26, -21, -17, -13, -9,
      -5, -2, 0,   2,   5,   9,   13, 17, 21, 26, 32};
  static const int inv_angle[] = {-4096, -1638, -910, -630,  -482,
                                  -390,  -315,  -256, -315,  -390,
                                  -482,  -630,  -910, -1638, -4096};

  const int angle = intra_pred_angle[intra_mode - 2];
  const int last = (size * angle) >> 5;
  if (intra_mode >= 18) {
    const int *ref = pred_top - 1;
    std::vector<int> ref_storage(3 * size + 1, default_sample);
    int *ref_tmp = ref_storage.data() + size;
    if (angle < 0 && last < -1) {
      for (int x = 0; x <= size; ++x) ref_tmp[x] = pred_top[x - 1];
      for (int x = last; x <= -1; ++x) {
        const int inv_idx =
            -1 + ((x * inv_angle[intra_mode - 11] + 128) >> 8);
        ref_tmp[x] = pred_left[inv_idx];
      }
      ref = ref_tmp;
    }

    for (int y = 0; y < block_h; ++y) {
      const int idx = ((y + 1) * angle) >> 5;
      const int fact = ((y + 1) * angle) & 31;
      for (int x = 0; x < block_w; ++x) {
        const int value =
            fact ? ((32 - fact) * ref[x + idx + 1] +
                        fact * ref[x + idx + 2] + 16) >>
                       5
                 : ref[x + idx + 1];
        store_pred(x, y, value);
      }
    }

    if (intra_mode == 26 && size < 32 && block_w > 0) {
      for (int y = 0; y < block_h; ++y)
        store_pred(0, y, pred_top[0] + ((pred_left[y] - pred_left[-1]) >> 1));
    }
  } else {
    const int *ref = pred_left - 1;
    std::vector<int> ref_storage(3 * size + 1, default_sample);
    int *ref_tmp = ref_storage.data() + size;
    if (angle < 0 && last < -1) {
      for (int x = 0; x <= size; ++x) ref_tmp[x] = pred_left[x - 1];
      for (int x = last; x <= -1; ++x) {
        const int inv_idx =
            -1 + ((x * inv_angle[intra_mode - 11] + 128) >> 8);
        ref_tmp[x] = pred_top[inv_idx];
      }
      ref = ref_tmp;
    }

    for (int x = 0; x < block_w; ++x) {
      const int idx = ((x + 1) * angle) >> 5;
      const int fact = ((x + 1) * angle) & 31;
      for (int y = 0; y < block_h; ++y) {
        const int value =
            fact ? ((32 - fact) * ref[y + idx + 1] +
                        fact * ref[y + idx + 2] + 16) >>
                       5
                 : ref[y + idx + 1];
        store_pred(x, y, value);
      }
    }

    if (intra_mode == 10 && size < 32 && block_h > 0) {
      for (int x = 0; x < block_w; ++x)
        store_pred(x, 0, pred_left[0] + ((pred_top[x] - pred_top[-1]) >> 1));
    }
  }
  log_pred_block();
  return 0;
}

int SliceData::prediction_unit(int x0, int y0, int nPbW, int nPbH) {
  const int ux = state_x(x0);
  const int uy = state_y(y0);
  int inter_pred_idc = PRED_L0;
  int mvp_l0_flag = 0;
  int mvp_l1_flag = 0;
  int ref_idx_l0 = 0;
  int ref_idx_l1 = 0;
  int MvdL1[2] = {0};

  if (cu_skip_flag[ux][uy]) {
    if (header->MaxNumMergeCand > 1) {
      const uint8_t merge_idx_val =
          cabac->ff_hevc_merge_idx_decode(header->MaxNumMergeCand); //ae(v);
      fill_block_map(merge_idx, x0, y0, nPbW, nPbH, merge_idx_val);
      std::cout << "merge_idx:" << (int)merge_idx[ux][uy] << std::endl;
    } else {
      fill_block_map(merge_idx, x0, y0, nPbW, nPbH, 0);
    }
  } else { /* MODE_INTER */
    const uint8_t merge_flag_val =
        cabac->decode_bin(elem_offset[MERGE_FLAG]); //ae(v);
    fill_block_map(merge_flag, x0, y0, nPbW, nPbH, merge_flag_val);
    std::cout << "merge_flag[x0][y0]:" << (int)merge_flag[ux][uy] << std::endl;
    if (merge_flag[ux][uy]) {
      if (header->MaxNumMergeCand > 1) {
        const uint8_t merge_idx_val =
            cabac->ff_hevc_merge_idx_decode(header->MaxNumMergeCand); //ae(v);
        fill_block_map(merge_idx, x0, y0, nPbW, nPbH, merge_idx_val);
        std::cout << "merge_idx[x0][y0]:" << (int)merge_idx[ux][uy] << std::endl;
      }
    } else {
      if (header->slice_type == HEVC_SLICE_B) {
        inter_pred_idc =
            cabac->ff_hevc_inter_pred_idc_decode(nPbW, nPbH, ct_depth); //ae(v);
        std::cout << "inter_pred_idc[x0][y0]:" << inter_pred_idc << std::endl;
      }
      if (inter_pred_idc != PRED_L1) {
        if (header->num_ref_idx_l0_active_minus1 > 0) {
          ref_idx_l0 = cabac->ff_hevc_ref_idx_lx_decode(
              header->num_ref_idx_l0_active_minus1 + 1); //ae(v);
          std::cout << "ref_idx_l0[x0][y0]:" << ref_idx_l0 << std::endl;
        }
        mvd_coding(x0, y0, 0);
        mvp_l0_flag = cabac->ff_hevc_mvp_lx_flag_decode(); //ae(v);
        std::cout << "mvp_l0_flag[x0][y0]:" << mvp_l0_flag << std::endl;
      }
      if (inter_pred_idc != PRED_L0) {
        if (header->num_ref_idx_l1_active_minus1 > 0) {
          ref_idx_l1 = cabac->ff_hevc_ref_idx_lx_decode(
              header->num_ref_idx_l1_active_minus1 + 1); //ae(v);
          std::cout << "ref_idx_l1[x0][y0]:" << ref_idx_l1 << std::endl;
        }
        if (header->mvd_l1_zero_flag && inter_pred_idc == PRED_BI) {
          MvdL1[0] = 0, MvdL1[1] = 0;
        } else
          mvd_coding(x0, y0, 1);
        mvp_l1_flag = cabac->ff_hevc_mvp_lx_flag_decode(); //ae(v);
        std::cout << "mvp_l1_flag[x0][y0]:" << mvp_l1_flag << std::endl;
      }
    }
  }
  return 0;
}

int SliceData::pcm_sample(int x0, int y0, int log2CbSize) {
  std::cout << "Into -> " << __FUNCTION__ << "():" << __LINE__ << std::endl;
  int i;
  uint8_t *pcm_sample_luma = new uint8_t[1 << (log2CbSize << 1)];
  uint8_t *pcm_sample_chroma = new uint8_t[1 << (log2CbSize << 1)];
  for (i = 0; i < 1 << (log2CbSize << 1); i++)
    pcm_sample_luma[i] = 0; //u(v); TODO  <24-12-15 18:09:51, YangJing>
  if (m_sps->ChromaArrayType != 0)
    for (i = 0; i < ((2 << (log2CbSize << 1)) /
                     (m_sps->SubWidthC * m_sps->SubHeightC));
         i++)
      pcm_sample_chroma[i] = 0; //u(v); TODO  <24-12-15 18:10:24, YangJing>

  return 0;
}

int SliceData::transform_tree(int x0, int y0, int xBase, int yBase, int cbXBase,
                              int cbYBase, int log2CbSize, int log2TrafoSize,
                              int trafoDepth, int blkIdx,
                              const int *base_cbf_cb,
                              const int *base_cbf_cr) {
  const int ux = state_x(x0);
  const int uy = state_y(y0);
  const int MinTbLog2SizeY = m_sps->log2_min_luma_transform_block_size;
  int cbf_cb[2] = {base_cbf_cb[0], base_cbf_cb[1]};
  int cbf_cr[2] = {base_cbf_cr[0], base_cbf_cr[1]};
  int split_transform_flag = 0;

  //初始化后续使用的字段
  if (IntraSplitFlag) {
    if (trafoDepth == 1) {
      m_tu.intra_pred_mode = m_pu.intra_pred_mode[blkIdx];
      if (m_sps->chroma_format_idc == 3) {
        m_tu.intra_pred_mode_c = m_pu.intra_pred_mode_c[blkIdx];
        m_tu.chroma_mode_c = m_pu.intra_chroma_pred_mode[blkIdx];
      } else {
        m_tu.intra_pred_mode_c = m_pu.intra_pred_mode_c[0];
        m_tu.chroma_mode_c = m_pu.intra_chroma_pred_mode[0];
      }
    }
  } else {
    m_tu.intra_pred_mode = m_pu.intra_pred_mode[0];
    m_tu.intra_pred_mode_c = m_pu.intra_pred_mode_c[0];
    m_tu.chroma_mode_c = m_pu.intra_chroma_pred_mode[0];
  }
  // split_transform_flag may be inferred in several branches, so assign every call.
  if (log2TrafoSize <= MaxTbLog2SizeY && log2TrafoSize > MinTbLog2SizeY &&
      trafoDepth < MaxTrafoDepth && !(IntraSplitFlag && (trafoDepth == 0))) {
    split_transform_flag = cabac->decode_bin(
        elem_offset[SPLIT_TRANSFORM_FLAG] + 5 - log2TrafoSize); // ae(v);
  } else {
    int inter_split = m_sps->max_transform_hierarchy_depth_inter == 0 &&
                      CuPredMode[ux][uy] == MODE_INTER &&
                      CurrPartMode != PART_2Nx2N && trafoDepth == 0;
    split_transform_flag = log2TrafoSize > MaxTbLog2SizeY ||
                           (IntraSplitFlag && trafoDepth == 0) ||
                           inter_split;
  }
  std::cout << "split_transform_flag:" << split_transform_flag << std::endl;
  if ((x0 < 16 && y0 < 16) ||
      (x0 >= 320 && x0 <= 544 && y0 >= 256 && y0 <= 544) ||
      (x0 >= 544 && y0 >= 544)) {
    std::cout << "DBG ours tu=(" << x0 << "," << y0 << ") log2="
              << log2TrafoSize << " depth=" << trafoDepth
              << " split=" << split_transform_flag << std::endl;
  }

  if ((log2TrafoSize > 2 && m_sps->ChromaArrayType != 0) ||
      m_sps->ChromaArrayType == 3) {
    if (trafoDepth == 0 || cbf_cb[0]) {
      cbf_cb[0] = cabac->decode_bin(elem_offset[CBF_CB_CR] + trafoDepth); // ae(v);
      printf("cbf_cb0:%d\n", cbf_cb[0]);
      if (m_sps->ChromaArrayType == 2 &&
          (!split_transform_flag || log2TrafoSize == 3)) {
        cbf_cb[1] = cabac->decode_bin(elem_offset[CBF_CB_CR] + trafoDepth); // ae(v);
        printf("cbf_cb1:%d\n", cbf_cb[1]);
      }
    }
    if (trafoDepth == 0 || cbf_cr[0]) {
      cbf_cr[0] = cabac->decode_bin(elem_offset[CBF_CB_CR] + trafoDepth); // ae(v);
      printf("cbf_cr0:%d\n", cbf_cr[0]);
      if (m_sps->ChromaArrayType == 2 &&
          (!split_transform_flag || log2TrafoSize == 3)) {
        cbf_cr[1] = cabac->decode_bin(elem_offset[CBF_CB_CR] + trafoDepth); // ae(v);
        printf("cbf_cr1:%d\n", cbf_cr[1]);
      }
    }
  }
  if (split_transform_flag) {
    int x1 = x0 + (1 << (log2TrafoSize - 1));
    int y1 = y0 + (1 << (log2TrafoSize - 1));
    transform_tree(x0, y0, x0, y0, cbXBase, cbYBase, log2CbSize,
                   log2TrafoSize - 1, trafoDepth + 1, 0, cbf_cb, cbf_cr);
    transform_tree(x1, y0, x0, y0, cbXBase, cbYBase, log2CbSize,
                   log2TrafoSize - 1, trafoDepth + 1, 1, cbf_cb, cbf_cr);
    transform_tree(x0, y1, x0, y0, cbXBase, cbYBase, log2CbSize,
                   log2TrafoSize - 1, trafoDepth + 1, 2, cbf_cb, cbf_cr);
    transform_tree(x1, y1, x0, y0, cbXBase, cbYBase, log2CbSize,
                   log2TrafoSize - 1, trafoDepth + 1, 3, cbf_cb, cbf_cr);
  } else {
    int cbf_luma = 1;
    if (CuPredMode[ux][uy] == MODE_INTRA || trafoDepth != 0 ||
        cbf_cb[0] || cbf_cr[0] ||
        (m_sps->ChromaArrayType == 2 && (cbf_cb[1] || cbf_cr[1])))
      cbf_luma = cabac->decode_bin(elem_offset[CBF_LUMA] + !trafoDepth); //ae(v);
    printf("cbf_luma:%d\n", cbf_luma);
    if ((x0 < 16 && y0 < 16) ||
        (x0 >= 320 && x0 <= 544 && y0 >= 256 && y0 <= 544) ||
        (x0 >= 544 && y0 >= 544)) {
      std::cout << "DBG ours tu_leaf=(" << x0 << "," << y0 << ") log2="
                << log2TrafoSize << " depth=" << trafoDepth
                << " cbf_luma=" << cbf_luma << " cbf_cb0=" << cbf_cb[0]
                << " cbf_cr0=" << cbf_cr[0] << std::endl;
    }
    transform_unit(x0, y0, xBase, yBase, cbXBase, cbYBase, log2CbSize,
                   log2TrafoSize, trafoDepth, blkIdx, cbf_luma, cbf_cb,
                   cbf_cr);
  }
  return 0;
}

int SliceData::mvd_coding(int x0, int y0, int refList) {
  int mvd[2] = {0};
  int x = cabac->abs_mvd_greater0_flag_decode();
  int y = cabac->abs_mvd_greater0_flag_decode();

  if (x) x += cabac->abs_mvd_greater1_flag_decode();
  if (y) y += cabac->abs_mvd_greater1_flag_decode();

  switch (x) {
  case 2:
    mvd[0] = cabac->mvd_decode();
    break;
  case 1:
    mvd[0] = cabac->mvd_sign_flag_decode();
    break;
  default:
    mvd[0] = 0;
    break;
  }

  switch (y) {
  case 2:
    mvd[1] = cabac->mvd_decode();
    break;
  case 1:
    mvd[1] = cabac->mvd_sign_flag_decode();
    break;
  default:
    mvd[1] = 0;
    break;
  }

  std::cout << "mvd_l" << refList << "[x,y]=(" << mvd[0] << "," << mvd[1]
            << ")" << std::endl;
  return 0;
}

int SliceData::transform_unit(int x0, int y0, int xBase, int yBase, int cbXBase,
                              int cbYBase, int log2CbSize, int log2TrafoSize,
                              int trafoDepth, int blkIdx, int cbfLuma,
                              const int *cbf_cb, const int *cbf_cr) {
  const int &ChromaArrayType = m_sps->ChromaArrayType;
  const int &MinCbLog2SizeY = m_sps->MinCbLog2SizeY;
  const int ux = state_x(x0);
  const int uy = state_y(y0);
  int tIdx = 0;
  int log2TrafoSizeC = MAX(2, log2TrafoSize - (ChromaArrayType == 3 ? 0 : 1));
  int cbfChroma =
      cbf_cb[0] || cbf_cr[0] || (ChromaArrayType == 2 && (cbf_cb[1] || cbf_cr[1]));
  const bool dbg_focus =
      (x0 < 16 && y0 < 16) ||
      (x0 >= 224 && x0 <= 544 && y0 >= 192 && y0 <= 544) ||
      (x0 >= 256 && y0 >= 448);
  /* TODO YangJing 这里的PartMode是乱写的，后面一定要确定 <25-01-01 22:01:36> */
  int PartMode = 1;

  if (CuPredMode[ux][uy] == MODE_INTRA)
    reconstruct_intra_luma_block(x0, y0, 1 << log2TrafoSize,
                                 m_tu.intra_pred_mode);

  if (cbfLuma || cbfChroma) {
    int xP = (x0 >> MinCbLog2SizeY) << MinCbLog2SizeY;
    int yP = (y0 >> MinCbLog2SizeY) << MinCbLog2SizeY;
    int nCbS = 1 << MinCbLog2SizeY;
    if (m_pps->residual_adaptive_colour_transform_enabled_flag &&
        (CuPredMode[ux][uy] == MODE_INTER ||
         (PartMode == PART_2Nx2N &&
          intra_chroma_pred_mode[state_x(x0)][state_y(y0)] == 4) ||
         (intra_chroma_pred_mode[state_x(xP)][state_y(yP)] == 4 &&
          intra_chroma_pred_mode[state_x(xP + nCbS / 2)][state_y(yP)] == 4 &&
          intra_chroma_pred_mode[state_x(xP)][state_y(yP + nCbS / 2)] == 4 &&
          intra_chroma_pred_mode[state_x(xP + nCbS / 2)]
                               [state_y(yP + nCbS / 2)] == 4))) {
      /* TODO YangJing  <25-01-01 21:58:43> */
      //tu_residual_act_flag[x0][y0] = ae(v);
      std::cout << "WARN: residual_adaptive_colour_transform is not fully "
                   "implemented, continue with default."
                << std::endl;
    }

    int scan_idx = SCAN_DIAG;
    int scan_idx_c = SCAN_DIAG;

    const bool need_qp_update =
        m_pps->cu_qp_delta_enabled_flag && !IsCuQpDeltaCoded;
    if (need_qp_update) {
      delta_qp();
      set_qPy(cbXBase, cbYBase, log2CbSize);
    }
    if (cbfChroma && !cu_transquant_bypass_flag) {
      chroma_qp_offset();
    }

    /* TODO YangJing 这里的scan_idx错误 <25-03-06 12:12:54> */
#if 1
    // For ffmpeg:
    if (CuPredMode[ux][uy] == MODE_INTRA && log2TrafoSize < 4) {
      if (m_tu.intra_pred_mode >= 6 && m_tu.intra_pred_mode <= 14) {
        scan_idx = Cabac::SCAN_VERT;
      } else if (m_tu.intra_pred_mode >= 22 && m_tu.intra_pred_mode <= 30) {
        scan_idx = Cabac::SCAN_HORIZ;
      }

      if (m_tu.intra_pred_mode_c >= 6 && m_tu.intra_pred_mode_c <= 14) {
        scan_idx_c = Cabac::SCAN_VERT;
      } else if (m_tu.intra_pred_mode_c >= 22 && m_tu.intra_pred_mode_c <= 30) {
        scan_idx_c = Cabac::SCAN_HORIZ;
      }
    }

    m_tu.cross_pf = 0;
#endif

    if (cbfLuma)
      residual_coding(x0, y0, log2TrafoSize, (Cabac::ScanType)scan_idx, 0);
    if (log2TrafoSize > 2 || ChromaArrayType == 3) {
      if (m_pps->cross_component_prediction_enabled_flag && cbfLuma &&
          (CuPredMode[ux][uy] == MODE_INTER ||
           intra_chroma_pred_mode[ux][uy] == 4))
        cross_comp_pred(x0, y0, 0);
      for (tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++)
        if (cbf_cb[tIdx])
          residual_coding(x0, y0 + (tIdx << log2TrafoSizeC), log2TrafoSizeC,
                          (Cabac::ScanType)scan_idx_c, 1);
      if (m_pps->cross_component_prediction_enabled_flag && cbfLuma &&
          (CuPredMode[ux][uy] == MODE_INTER ||
           intra_chroma_pred_mode[ux][uy] == 4))
        cross_comp_pred(x0, y0, 1);
      for (tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++)
        if (cbf_cr[tIdx])
          residual_coding(x0, y0 + (tIdx << log2TrafoSizeC), log2TrafoSizeC,
                          (Cabac::ScanType)scan_idx_c, 2);
    } else if (blkIdx == 3) {
      for (tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++)
        if (cbf_cb[tIdx])
          residual_coding(xBase, yBase + (tIdx << log2TrafoSizeC),
                          log2TrafoSize, (Cabac::ScanType)scan_idx_c, 1);
      for (tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++)
        if (cbf_cr[tIdx])
          residual_coding(xBase, yBase + (tIdx << log2TrafoSizeC),
                          log2TrafoSize, (Cabac::ScanType)scan_idx_c, 2);
    }
  }
  if (dbg_focus && CuPredMode[ux][uy] == MODE_INTRA && pic &&
      pic->m_pic_buff_luma != nullptr) {
    const int width = pic->PicWidthInSamplesL;
    const int height = pic->PicHeightInSamplesL;
    const int size = 1 << log2TrafoSize;
    const int block_w = MIN(size, MAX(0, width - x0));
    const int block_h = MIN(size, MAX(0, height - y0));
    std::cout << "DBG ours final_block pos=(" << x0 << "," << y0
              << ") size=" << size << " mode=" << m_tu.intra_pred_mode
              << " cbf_luma=" << cbfLuma << " qp_y=" << qp_y << std::endl;
    for (int row = 0; row < block_h; ++row) {
      std::cout << "DBG ours final_row pos=(" << x0 << "," << y0
                << ") row=" << row << " vals=";
      for (int col = 0; col < block_w; ++col) {
        if (col) std::cout << ",";
        std::cout << (int)pic->m_pic_buff_luma[(y0 + row) * width + x0 + col];
      }
      std::cout << std::endl;
    }
  }
  return 0;
}

int SliceData::residual_coding(int x0, int y0, int log2TrafoSize,
                               Cabac::ScanType scanIdx, int cIdx) {
  const int ux = state_x(x0);
  const int uy = state_y(y0);
  const int trafoSize = 1 << log2TrafoSize;
  std::vector<int32_t> coeff_levels(trafoSize * trafoSize, 0);
  static int yangjing = 0;
  yangjing++;
  if (yangjing > 1) {
    std::cout << "yangjing = " << yangjing << std::endl;
  }
  int scale_m;
  uint8_t transform_skip_flag = 0;
  uint8_t explicit_rdpcm_flag = 0;
  uint8_t explicit_rdpcm_dir_flag = 0;
  int xS, yS, xC, yC;
  int greater1_ctx = 1;
  const bool dbg_focus =
      ((x0 < 16 && y0 < 16) ||
       (x0 >= 224 && x0 <= 544 && y0 >= 192 && y0 <= 544) ||
       (x0 >= 544 && y0 >= 544)) &&
      cIdx == 0;

  if (m_pps->transform_skip_enabled_flag && !cu_transquant_bypass_flag &&
      (log2TrafoSize <= m_pps->Log2MaxTransformSkipSize))
    transform_skip_flag =
        cabac->decode_bin(elem_offset[TRANSFORM_SKIP_FLAG] + !!cIdx);

  if (CuPredMode[ux][uy] == MODE_INTER && m_sps->explicit_rdpcm_enabled_flag &&
      (transform_skip_flag || cu_transquant_bypass_flag)) {
    explicit_rdpcm_flag =
        cabac->decode_bin(elem_offset[EXPLICIT_RDPCM_FLAG] + !!cIdx); //ae(v);
    std::cout << "explicit_rdpcm_flag[x0][y0][cIdx]:"
              << (int)explicit_rdpcm_flag << std::endl;
    if (explicit_rdpcm_flag) {
      explicit_rdpcm_dir_flag = cabac->decode_bin(
          elem_offset[EXPLICIT_RDPCM_DIR_FLAG] + !!cIdx); //ae(v);
      std::cout << "explicit_rdpcm_dir_flag[x0][y0][cIdx]:"
                << (int)explicit_rdpcm_dir_flag << std::endl;
    }
  }

  int last_sig_coeff_x_prefix; //ae(v);
  int last_sig_coeff_y_prefix; //ae(v);
  int last_sig_coeff_x_suffix, last_sig_coeff_y_suffix;
  int LastSignificantCoeffX, LastSignificantCoeffY;
  printf("call last_significant_coeff_xy_prefix_decode(%d,%d,,)\n", cIdx,
         log2TrafoSize);
  cabac->last_significant_coeff_xy_prefix_decode(
      cIdx, log2TrafoSize, &last_sig_coeff_x_prefix, &last_sig_coeff_y_prefix);

  if (last_sig_coeff_x_prefix > 3) {
    int suffix =
        cabac->last_significant_coeff_suffix_decode(last_sig_coeff_x_prefix);
    LastSignificantCoeffX = last_sig_coeff_x_suffix =
        (1 << ((last_sig_coeff_x_prefix >> 1) - 1)) *
            (2 + (last_sig_coeff_x_prefix & 1)) +
        suffix; //ae(v);
  } else {
    LastSignificantCoeffX = last_sig_coeff_x_prefix;
  }

  if (last_sig_coeff_y_prefix > 3) {
    int suffix =
        cabac->last_significant_coeff_suffix_decode(last_sig_coeff_y_prefix);
    LastSignificantCoeffY = last_sig_coeff_y_suffix =
        (1 << ((last_sig_coeff_y_prefix >> 1) - 1)) *
            (2 + (last_sig_coeff_y_prefix & 1)) +
        suffix;
  } else {
    LastSignificantCoeffY = last_sig_coeff_y_prefix;
  }

  std::cout << "LastSignificantCoeffX:" << LastSignificantCoeffX << std::endl;
  std::cout << "LastSignificantCoeffY:" << LastSignificantCoeffY << std::endl;
  if (dbg_focus) {
    std::cout << "DBG ours residual_start pos=(" << x0 << "," << y0
              << ") log2=" << log2TrafoSize << " scan=" << scanIdx
              << " last=(" << LastSignificantCoeffX << ","
              << LastSignificantCoeffY << ")" << std::endl;
  }
  if (LastSignificantCoeffX == 5 && LastSignificantCoeffY == 0) {
    int a = 0;
  }

#define FFSWAP(type, a, b)                                                     \
  do {                                                                         \
    type SWAP_tmp = b;                                                         \
    b = a;                                                                     \
    a = SWAP_tmp;                                                              \
  } while (0)
  printf("scanIdx:%d\n", scanIdx);
  //When scanIdx is equal to 2, the coordinates are swapped as follows:
  if (scanIdx == Cabac::SCAN_VERT)
    FFSWAP(int, LastSignificantCoeffX, LastSignificantCoeffY);

  /* TODO YangJing  <25-01-26 10:03:32> */
  // ScanOrder[log2BlockSize][scanIdx][sPos][sComp], log2BlockSize in [0,3].
  uint8_t ScanOrder[4][3][64][2] = {0};

  //对于从 0 到 3（含）的 log2BlockSize，使用 1 << log2BlockSize 作为输入调用第 6.5.3 节中指定的右上对角线扫描顺序数组初始化过程，并将输出分配给 ScanOrder[ log2BlockSize ][ 0 ] 。
  //=============================================不确定是否对
  for (int log2BlockSize = 0; log2BlockSize <= 3; ++log2BlockSize) {
    Up_right_diagonal_scan_order_array_initialization_process(
        1 << log2BlockSize, ScanOrder[log2BlockSize][Cabac::SCAN_DIAG]);
    Horizontal_scan_order_array_initialization_process(
        1 << log2BlockSize, ScanOrder[log2BlockSize][Cabac::SCAN_HORIZ]);
    Vertical_scan_order_array_initialization_process(
        1 << log2BlockSize, ScanOrder[log2BlockSize][Cabac::SCAN_VERT]);
  }

  //std::cout << std::endl;
  //for (int i = 0; i < 16; ++i) {
  //cout << (int)ScanOrder[2][scanIdx][i][0] << ",";
  //}
  //std::cout << std::endl;

  //std::cout << std::endl;
  //for (int i = 0; i < 16; ++i) {
  //cout << (int)ScanOrder[2][scanIdx][i][1] << ",";
  //}
  //std::cout << std::endl;
  //=============================================

  // scanIdx等于0指定右上对角扫描顺序，scanIdx等于1指定水平扫描顺序，scanIdx等于2指定垂直扫描顺序，scanIdx等于3指定横向扫描顺序
  int lastScanPos = 16; //lastScanPos 表示在 4x4 子块内的扫描位置
  //最后一个子块的索引
  int lastSubBlock =
      (1 << (log2TrafoSize - 2)) * (1 << (log2TrafoSize - 2)) - 1;
  do {
    if (lastScanPos == 0) {
      lastScanPos = 16;
      lastSubBlock--;
    }
    lastScanPos--;
    xS = ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock][0];
    yS = ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock][1];
    xC = (xS << 2) + ScanOrder[2][scanIdx][lastScanPos][0];
    yC = (yS << 2) + ScanOrder[2][scanIdx][lastScanPos][1];
  } while ((xC != LastSignificantCoeffX) || (yC != LastSignificantCoeffY));
  printf("lastScanPos:%d,LastSignificantCoeffX:%d,LastSignificantCoeffY:%d,xC:%"
         "d,yC:%d\n",
         lastScanPos, LastSignificantCoeffX, LastSignificantCoeffY, xC, yC);

  int i, n;
  uint8_t coded_sub_block_flag[32][32] = {0};
  uint8_t sig_coeff_flag[32][32] = {0};

  int x_cg_last_sig = LastSignificantCoeffX >> 2;
  int y_cg_last_sig = LastSignificantCoeffY >> 2;
  const int cg_size = 1 << MAX(log2TrafoSize - 2, 0);

  for (i = lastSubBlock; i >= 0; i--) {
    uint8_t significant_coeff_flag_idx[16];
    uint8_t nb_significant_coeff_flag = 0;

    int rice_init = 0;
    int pos;
    xS = ScanOrder[log2TrafoSize - 2][scanIdx][i][0];
    yS = ScanOrder[log2TrafoSize - 2][scanIdx][i][1];
    int escapeDataPresent = 0;
    int inferSbDcSigCoeffFlag = 0;
    if ((i < lastSubBlock) && (i > 0)) {
      int ctx_cg = 0;
      if (xS < cg_size - 1) ctx_cg += !!coded_sub_block_flag[xS + 1][yS];
      if (yS < cg_size - 1) ctx_cg += !!coded_sub_block_flag[xS][yS + 1];
      inferSbDcSigCoeffFlag = 1;
      coded_sub_block_flag[xS][yS] =
          cabac->significant_coeff_group_flag_decode(cIdx, ctx_cg); //ae(v);
    } else {
      //NOTE: 来自ffmpeg，HEVC中没有这段
      coded_sub_block_flag[xS][yS] =
          ((xS == x_cg_last_sig && yS == y_cg_last_sig) ||
           (xS == 0 && yS == 0));
    }

    if (i == lastSubBlock) {
      significant_coeff_flag_idx[0] = lastScanPos;
      nb_significant_coeff_flag = 1;
    }

    printf("n_end:%d\n", lastScanPos - 1);

    int prev_sig = 0;
    if (xS < ((1 << log2TrafoSize) - 1) >> 2)
      prev_sig = !!coded_sub_block_flag[xS + 1][yS];
    if (yS < ((1 << log2TrafoSize) - 1) >> 2)
      prev_sig += (!!coded_sub_block_flag[xS][yS + 1] << 1);

    uint8_t *ctx_idx_map_p;
    int scf_offset = 0;
    get_scf_offse(scf_offset, ctx_idx_map_p, transform_skip_flag,
                  cIdx, log2TrafoSize, xS, yS, scanIdx, prev_sig);

    for (n = (i == lastSubBlock) ? lastScanPos - 1 : 15; n >= 0; n--) {
      const int localXC = ScanOrder[2][scanIdx][n][0];
      const int localYC = ScanOrder[2][scanIdx][n][1];
      xC = (xS << 2) + localXC;
      yC = (yS << 2) + localYC;
      if (coded_sub_block_flag[xS][yS] && (n > 0 || !inferSbDcSigCoeffFlag)) {
        if (n == 0) {
          get_scf_offse0(scf_offset, ctx_idx_map_p,
                         transform_skip_flag, cIdx, log2TrafoSize,
                         xS, yS, scanIdx, prev_sig, i);
          sig_coeff_flag[xC][yC] = cabac->decode_bin(
              elem_offset[SIGNIFICANT_COEFF_FLAG] + scf_offset); //ae(v);
          if (sig_coeff_flag[xC][yC]) {
            significant_coeff_flag_idx[nb_significant_coeff_flag] = 0;
            nb_significant_coeff_flag++;
            inferSbDcSigCoeffFlag = 0;
          }

        } else {
          sig_coeff_flag[xC][yC] =
              cabac->significant_coeff_flag_decode(localXC, localYC, scf_offset,
                                                   ctx_idx_map_p); //ae(v);
          if (sig_coeff_flag[xC][yC]) {
            significant_coeff_flag_idx[nb_significant_coeff_flag] = n;
            nb_significant_coeff_flag++;
            inferSbDcSigCoeffFlag = 0;
          }
        }
      }
      printf("sig_coeff_flag:%d,xC:%d,yC:%d\n", sig_coeff_flag[xC][yC], xC, yC);
    }
    if (coded_sub_block_flag[xS][yS] && inferSbDcSigCoeffFlag &&
        nb_significant_coeff_flag < 16) {
      const int dcX = xS << 2;
      const int dcY = yS << 2;
      sig_coeff_flag[dcX][dcY] = 1;
      significant_coeff_flag_idx[nb_significant_coeff_flag] = 0;
      nb_significant_coeff_flag++;
    }

    const int n_end = nb_significant_coeff_flag;
    if (n_end == 0) continue;

    int ctx_set = (i > 0 && cIdx == 0) ? 2 : 0;
    int c_rice_param = 0;
    int first_greater1_coeff_idx = -1;
    int sbType = 0;
    if (!(i == lastSubBlock) && greater1_ctx == 0) ctx_set++;
    greater1_ctx = 1;

    if (m_sps->persistent_rice_adaptation_enabled_flag) {
      if (!transform_skip_flag && !cu_transquant_bypass_flag)
        sbType = 2 * (cIdx == 0 ? 1 : 0);
      else
        sbType = 2 * (cIdx == 0 ? 1 : 0) + 1;
      c_rice_param = StatCoeff[sbType] / 4;
    }

    uint8_t coeff_abs_level_greater1_flag[16] = {0};
    uint8_t coeff_abs_level_greater2_flag[16] = {0};
    uint8_t coeff_abs_level_remaining[16] = {0};

    const int lastSigScanPos = significant_coeff_flag_idx[0];
    const int firstSigScanPos = significant_coeff_flag_idx[n_end - 1];

    for (int m = 0; m < MIN(8, n_end); ++m) {
      const int inc = (ctx_set << 2) + greater1_ctx;
      coeff_abs_level_greater1_flag[m] =
          cabac->coeff_abs_level_greater1_flag_decode(cIdx, inc); // ae(v)
      printf("coeff_abs_level_greater1_flag:%d,cIdx:%d,inc:%d\n",
             coeff_abs_level_greater1_flag[m], cIdx, inc);
      if (coeff_abs_level_greater1_flag[m]) {
        greater1_ctx = 0;
        if (first_greater1_coeff_idx == -1) first_greater1_coeff_idx = m;
      } else if (greater1_ctx > 0 && greater1_ctx < 3) {
        greater1_ctx++;
      }
    }

    int predModeIntra =
        (cIdx == 0) ? m_tu.intra_pred_mode : m_tu.intra_pred_mode_c;
    int signHidden;
    if (cu_transquant_bypass_flag ||
        (CuPredMode[ux][uy] == MODE_INTRA &&
         m_sps->implicit_rdpcm_enabled_flag &&
         transform_skip_flag &&
         (predModeIntra == 10 || predModeIntra == 26)) ||
        explicit_rdpcm_flag) {
      signHidden = 0;
    } else {
      signHidden = (lastSigScanPos - firstSigScanPos) >= 4;
    }

    if (first_greater1_coeff_idx != -1) {
      coeff_abs_level_greater2_flag[first_greater1_coeff_idx] =
          cabac->coeff_abs_level_greater2_flag_decode(cIdx, ctx_set); // ae(v)
      printf("coeff_abs_level_greater2_flag:%d\n",
             coeff_abs_level_greater2_flag[first_greater1_coeff_idx]);
    }
    uint16_t coeff_sign_flags = 0;
    if (!m_pps->sign_data_hiding_enabled_flag || !signHidden) {
      coeff_sign_flags =
          cabac->coeff_sign_flag_decode(n_end) << (16 - n_end); // ae(v)
    } else {
      coeff_sign_flags =
          cabac->coeff_sign_flag_decode(n_end - 1) << (16 - (n_end - 1)); //ae(v)
    }
    printf("coeff_sign_flag:%d,nb_significant_coeff_flag:%d\n", coeff_sign_flags,
           n_end);
    if (dbg_focus) {
      std::cout << "DBG ours level_ctx pos=(" << x0 << "," << y0
                << ") n_end=" << n_end
                << " first_g1=" << first_greater1_coeff_idx
                << " signHidden=" << signHidden
                << " signFlags=" << coeff_sign_flags << std::endl;
      for (int dbg_m = 0; dbg_m < n_end && dbg_m < 8; ++dbg_m) {
        std::cout << "DBG ours g1 pos=(" << x0 << "," << y0 << ") m="
                  << dbg_m << " val="
                  << (int)coeff_abs_level_greater1_flag[dbg_m] << std::endl;
      }
      if (first_greater1_coeff_idx != -1) {
        std::cout << "DBG ours g2 pos=(" << x0 << "," << y0 << ") m="
                  << first_greater1_coeff_idx << " val="
                  << (int)coeff_abs_level_greater2_flag[first_greater1_coeff_idx]
                  << std::endl;
      }
    }

    int sumAbsLevel = 0;
    for (int m = 0; m < n_end; ++m) {
      const int scanPos = significant_coeff_flag_idx[m];
      xC = (xS << 2) + ScanOrder[2][scanIdx][scanPos][0];
      yC = (yS << 2) + ScanOrder[2][scanIdx][scanPos][1];

      int transCoeffLevel = 0;
      int baseLevel = 0;
      if (m < 8) {
        baseLevel = 1 + coeff_abs_level_greater1_flag[m] +
                    coeff_abs_level_greater2_flag[m];
        const int threshold = (m == first_greater1_coeff_idx) ? 3 : 2;
        if (baseLevel == threshold) {
          coeff_abs_level_remaining[m] =
              cabac->coeff_abs_level_remaining_decode(c_rice_param); // ae(v)
          printf("coeff_abs_level_remaining:%d,c_rice_param:%d\n",
                 coeff_abs_level_remaining[m], c_rice_param);
          baseLevel += coeff_abs_level_remaining[m];
          if (baseLevel > (3 << c_rice_param)) {
            c_rice_param = m_sps->persistent_rice_adaptation_enabled_flag
                               ? c_rice_param + 1
                               : MIN(c_rice_param + 1, 4);
          }
          if (m_sps->persistent_rice_adaptation_enabled_flag && !rice_init) {
            const int initRiceValue = StatCoeff[sbType] / 4;
            if (coeff_abs_level_remaining[m] >= (3 << initRiceValue))
              StatCoeff[sbType]++;
            else if (2 * coeff_abs_level_remaining[m] < (1 << initRiceValue))
              if (StatCoeff[sbType] > 0) StatCoeff[sbType]--;
            rice_init = 1;
          }
        }
        transCoeffLevel = baseLevel;
      } else {
        coeff_abs_level_remaining[m] =
            cabac->coeff_abs_level_remaining_decode(c_rice_param); // ae(v)
        printf("coeff_abs_level_remaining:%d,c_rice_param:%d\n",
               coeff_abs_level_remaining[m], c_rice_param);
        transCoeffLevel = 1 + coeff_abs_level_remaining[m];
        if (transCoeffLevel > (3 << c_rice_param)) {
          c_rice_param = m_sps->persistent_rice_adaptation_enabled_flag
                             ? c_rice_param + 1
                             : MIN(c_rice_param + 1, 4);
        }
        if (m_sps->persistent_rice_adaptation_enabled_flag && !rice_init) {
          const int initRiceValue = StatCoeff[sbType] / 4;
          if (coeff_abs_level_remaining[m] >= (3 << initRiceValue))
            StatCoeff[sbType]++;
          else if (2 * coeff_abs_level_remaining[m] < (1 << initRiceValue))
            if (StatCoeff[sbType] > 0) StatCoeff[sbType]--;
          rice_init = 1;
        }
        baseLevel = transCoeffLevel;
      }

      if (m_pps->sign_data_hiding_enabled_flag && signHidden) {
        sumAbsLevel += transCoeffLevel;
        if (scanPos == firstSigScanPos && (sumAbsLevel & 1))
          transCoeffLevel = -transCoeffLevel;
      }

      if (coeff_sign_flags >> 15) transCoeffLevel = -transCoeffLevel;
      coeff_sign_flags <<= 1;

      if (dbg_focus) {
        std::cout << "DBG ours coeff raw pos=(" << x0 << "," << y0
                  << ") xy=(" << xC << "," << yC << ") level="
                  << transCoeffLevel << " scanPos=" << scanPos
                  << " m=" << m << std::endl;
      }

      if (xC >= 0 && xC < trafoSize && yC >= 0 && yC < trafoSize)
        coeff_levels[yC * trafoSize + xC] = transCoeffLevel;

      printf("TransCoeffLevel:%d,index:%d\n", transCoeffLevel, m);
    }
  }

  if (cIdx == 0 && CuPredMode[ux][uy] == MODE_INTRA && pic != nullptr &&
      pic->m_pic_buff_luma != nullptr && trafoSize >= 4 && trafoSize <= 32) {
    static const int kLevelScale[6] = {40, 45, 51, 57, 64, 72};
    const int bit_depth = m_sps->BitDepthY;
    const int qp = qp_y + m_sps->qp_bd_offset;
    const int shift = bit_depth + log2TrafoSize - 5;
    const int64_t add = shift > 0 ? (1LL << (shift - 1)) : 0;
    const int64_t scale =
        static_cast<int64_t>(kLevelScale[qp % 6]) << (qp / 6);

    std::vector<int32_t> dequantized(coeff_levels.size(), 0);
    for (size_t i = 0; i < coeff_levels.size(); ++i) {
      const int64_t scaled =
          static_cast<int64_t>(coeff_levels[i]) * scale * 16;
      const int64_t value =
          shift > 0 ? ((scaled + add) >> shift) : (scaled << (-shift));
      dequantized[i] = clip_int16_value(static_cast<int>(value));
    }

    std::vector<int32_t> residual;
    if (trafoSize == 4) {
      hevc_inverse_dst_4x4_luma(dequantized, bit_depth, residual);
    } else {
      hevc_inverse_dct_2d(dequantized, trafoSize, bit_depth, residual);
    }
    if (dbg_focus) {
      std::cout << "DBG ours residual_block pos=(" << x0 << "," << y0
                << ") size=" << trafoSize
                << " tsf=" << (int)transform_skip_flag
                << " explicit=" << (int)explicit_rdpcm_flag << std::endl;
      for (int row = 0; row < trafoSize; ++row) {
        std::cout << "DBG ours residual_row pos=(" << x0 << "," << y0
                  << ") row=" << row << " vals=";
        for (int col = 0; col < trafoSize; ++col) {
          if (col) std::cout << ",";
          std::cout << residual[row * trafoSize + col];
        }
        std::cout << std::endl;
      }
    }
    hevc_add_residual_to_luma(pic, x0, y0, trafoSize, bit_depth, residual);
  }
  return 0;
}

int SliceData::cross_comp_pred(int x0, int y0, int c) {
  log2_res_scale_abs_plus1[c] = cabac->ff_hevc_log2_res_scale_abs(c); //ae(v);
  if (log2_res_scale_abs_plus1[c] != 0)
    res_scale_sign_flag[c] = cabac->ff_hevc_res_scale_sign_flag(c); // ae(v);
  return 0;
}

int SliceData::palette_coding(int x0, int y0, int nCbS) {
  std::cout << "Into -> " << __FUNCTION__ << "():" << __LINE__ << std::endl;
  // TODO: Main profile 流暂不进入该路径，保留占位实现。
  //  palettePredictionFinished = 0;
  //  NumPredictedPaletteEntries = 0;
  //  for (predictorEntryIdx = 0;
  //       predictorEntryIdx < PredictorPaletteSize && !palettePredictionFinished &&
  //       NumPredictedPaletteEntries < palette_max_size;
  //       predictorEntryIdx++) {
  //    palette_predictor_run = ae(v);
  //    if (palette_predictor_run != 1) {
  //      if (palette_predictor_run > 1)
  //        predictorEntryIdx += palette_predictor_run - 1;
  //      PalettePredictorEntryReuseFlags[predictorEntryIdx] = 1;
  //      NumPredictedPaletteEntries++
  //    } else
  //      palettePredictionFinished = 1;
  //  }
  //  if (NumPredictedPaletteEntries < palette_max_size)
  //    num_signalled_palette_entries = ae(v);
  //  numComps = (ChromaArrayType == 0) ? 1 : 3;
  //  for (cIdx = 0; cIdx < numComps; cIdx++)
  //    for (i = 0; i < num_signalled_palette_entries; i++)
  //      new_palette_entries[cIdx][i] = ae(v);
  //  if (CurrentPaletteSize != 0) palette_escape_val_present_flag = ae(v);
  //  if (MaxPaletteIndex > 0) {
  //    num_palette_indices_minus1 = ae(v);
  //    adjust = 0;
  //    for (i = 0; i <= num_palette_indices_minus1; i++) {
  //      if (MaxPaletteIndex - adjust > 0) {
  //        palette_idx_idc = ae(v);
  //        PaletteIndexIdc[i] = palette_idx_idc;
  //      }
  //      adjust = 1;
  //    }
  //    copy_above_indices_for_final_run_flag = ae(v);
  //    palette_transpose_flag = ae(v);
  //  }
  //  if (palette_escape_val_present_flag) {
  //    delta_qp();
  //    if (!cu_transquant_bypass_flag) chroma_qp_offset();
  //  }
  //  remainingNumIndices = num_palette_indices_minus1 + 1;
  //  PaletteScanPos = 0;
  //  log2BlockSize = Log2(nCbS);
  //  while (PaletteScanPos < nCbS * nCbS) {
  //    xC = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos][0];
  //    yC = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos][1];
  //    if (PaletteScanPos > 0) {
  //      xcPrev = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos - 1][0];
  //      ycPrev = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos - 1][1]
  //    }
  //    PaletteRunMinus1 = nCbS * nCbS - PaletteScanPos - 1;
  //    RunToEnd = 1 CopyAboveIndicesFlag[xC][yC] = 0;
  //    if (MaxPaletteIndex > 0)
  //      if (PaletteScanPos >= nCbS && CopyAboveIndicesFlag[xcPrev][ycPrev] == 0)
  //        if (remainingNumIndices > 0 && PaletteScanPos < nCbS * nCbS - 1) {
  //          copy_above_palette_indices_flag = ae(v);
  //          CopyAboveIndicesFlag[xC][yC] = copy_above_palette_indices_flag
  //        } else if (PaletteScanPos == nCbS * nCbS - 1 && remainingNumIndices > 0)
  //          CopyAboveIndicesFlag[xC][yC] = 0;
  //        else
  //          CopyAboveIndicesFlag[xC][yC] = 1;
  //    if (CopyAboveIndicesFlag[xC][yC] == 0) {
  //      currNumIndices = num_palette_indices_minus1 + 1 - remainingNumIndices;
  //      CurrPaletteIndex = PaletteIndexIdc[currNumIndices];
  //    }
  //    if (MaxPaletteIndex > 0) {
  //      if (CopyAboveIndicesFlag[xC][yC] == 0) remainingNumIndices - = 1;
  //      if (remainingNumIndices > 0 ||
  //          CopyAboveIndicesFlag[xC][yC] !=
  //              copy_above_indices_for_final_run_flag) {
  //        PaletteMaxRunMinus1 = nCbS * nCbS - PaletteScanPos - 1 -
  //                              remainingNumIndices -
  //                              copy_above_indices_for_final_run_flag;
  //        RunToEnd = 0;
  //        if (PaletteMaxRunMinus1 > 0) {
  //          palette_run_prefix = ae(v);
  //          if ((palette_run_prefix > 1) &&
  //              (PaletteMaxRunMinus1 != (1 << (palette_run_prefix - 1))))
  //            palette_run_suffix = ae(v);
  //        }
  //      }
  //    }
  //    runPos = 0 while (runPos <= PaletteRunMinus1) {
  //      xR = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos][0];
  //      yR = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos][1];
  //      if (CopyAboveIndicesFlag[xC][yC] == 0) {
  //        CopyAboveIndicesFlag[xR][yR] = 0;
  //        PaletteIndexMap[xR][yR] = CurrPaletteIndex;
  //      } else {
  //        CopyAboveIndicesFlag[xR][yR] = 1;
  //        PaletteIndexMap[xR][yR] = PaletteIndexMap[xR][yR - 1];
  //      }
  //      runPos++;
  //      PaletteScanPos++;
  //    }
  //  }
  //  if (palette_escape_val_present_flag) {
  //    for (cIdx = 0; cIdx < numComps; cIdx++)
  //      for (sPos = 0; sPos < nCbS * nCbS; sPos++) {
  //        xC = x0 + ScanOrder[log2BlockSize][3][sPos][0];
  //        yC = y0 + ScanOrder[log2BlockSize][3][sPos][1];
  //        if (PaletteIndexMap[xC][yC] == MaxPaletteIndex)
  //          if (cIdx == 0 | |
  //                  (xC % 2 == 0 && yC % 2 == 0 && ChromaArrayType == 1) ||
  //              (xC % 2 == 0 && !palette_transpose_flag &&
  //               ChromaArrayType == 2) ||
  //              (yC % 2 == 0 && palette_transpose_flag && ChromaArrayType == 2) ||
  //              ChromaArrayType == 3) {
  //            palette_escape_val = ae(v);
  //            PaletteEscapeVal[cIdx][xC][yC] = palette_escape_val;
  //          }
  //      }
  //  }
  return 0;
}
//
int SliceData::delta_qp() {
  if (m_pps->cu_qp_delta_enabled_flag && !IsCuQpDeltaCoded) {
    IsCuQpDeltaCoded = 1;
    int cu_qp_delta_abs = cabac->ff_hevc_cu_qp_delta_abs(); //ae(v);
    printf("cu_qp_delta_abs:%d\n", cu_qp_delta_abs);
    if (cu_qp_delta_abs) {
      int cu_qp_delta_sign_flag = cabac->ff_decode_bypass(); //ae(v);
      printf("cu_qp_delta_sign_flag:%d\n", cu_qp_delta_sign_flag);
      CuQpDeltaVal = cu_qp_delta_abs * (1 - 2 * cu_qp_delta_sign_flag);
    }
  }
  return 0;
}

int SliceData::chroma_qp_offset() {
  if (header->cu_chroma_qp_offset_enabled_flag && !IsCuChromaQpOffsetCoded) {
    int cu_chroma_qp_offset_flag =
        cabac->decode_bin(elem_offset[CU_CHROMA_QP_OFFSET_FLAG]); //ae(v);
    if (cu_chroma_qp_offset_flag && m_pps->chroma_qp_offset_list_len_minus1 > 0)
      int cu_chroma_qp_offset_idx = cabac->ff_hevc_cu_chroma_qp_offset_idx(
          m_pps->chroma_qp_offset_list_len_minus1); //ae(v);
  }
  return 0;
}

int SliceData::slice_decoding_process() {
  /* 在场编码时可能存在多个slice data，只需要对首个slice data进行定位，同时在下面的操作中，只需要在首次进入Slice时才需要执行的 */
  if (pic->m_slice_cnt == 0) {
    /* 解码参考帧重排序(POC) */
    // 8.2.1 Decoding process for picture order count
    pic->decoding_picture_order_count(m_sps->pic_order_cnt_type);
    if (m_sps->frame_mbs_only_flag == 0) {
      /* 存在场宏块 */
      pic->m_parent->m_picture_top_filed.copyDataPicOrderCnt(*pic);
      //顶（底）场帧有可能被选为参考帧，在解码P/B帧时，会用到PicOrderCnt字段，所以需要在此处复制一份
      pic->m_parent->m_picture_bottom_filed.copyDataPicOrderCnt(*pic);
    }

    /* 8.2.2 Decoding process for macroblock to slice group map */
    decoding_macroblock_to_slice_group_map();

    // 8.2.4 Decoding process for reference picture lists construction
    if (header->slice_type == SLICE_P || header->slice_type == SLICE_SP ||
        header->slice_type == SLICE_B) {
      /* 当前帧需要参考帧预测，则需要进行参考帧重排序。在每个 P、SP 或 B Slice的解码过程开始时调用 */
      pic->decoding_ref_picture_lists_construction(
          pic->m_dpb, pic->m_RefPicList0, pic->m_RefPicList1);

      /* (m_RefPicList0,m_RefPicList1为m_dpb排序后的前后参考列表）打印帧重排序先后信息 */
      printFrameReorderPriorityInfo();
    }
  }
  pic->m_slice_cnt++;
  return 0;
}

// 8.2.2 Decoding process for macroblock to slice group map
/* 输入:活动图像参数集和要解码的Slice header。  
 * 输出:宏块到Slice Group映射MbToSliceGroupMap。 */
//该过程在每个Slice开始时调用（如果是单帧由单个Slice组成的情况，那么这里几乎没有逻辑）
inline int SliceData::decoding_macroblock_to_slice_group_map() {
  //输出为：mapUnitToSliceGroupMap
  mapUnitToSliceGroupMap();
  //输入为：mapUnitToSliceGroupMap
  mbToSliceGroupMap();
  return 0;
}

//8.2.2.1 - 8.2.2.7  Specification for interleaved slice group map type
inline int SliceData::mapUnitToSliceGroupMap() {
  /* 输入 */
  const int &MapUnitsInSliceGroup0 = header->MapUnitsInSliceGroup0;
  /* 输出 */
  int32_t *&mapUnitToSliceGroupMap = header->mapUnitToSliceGroupMap;

  /* mapUnitToSliceGroupMap 数组的推导如下：
   * – 如果 num_slice_groups_minus1 等于 0，则为范围从 0 到 PicSizeInMapUnits - 1（含）的所有 i 生成Slice Group映射的映射单元，如 mapUnitToSliceGroupMap[ i ] = 0 */
  /* 整个图像只被分为一个 slice group */
  if (m_pps->num_slice_groups_minus1 == 0) {
    /* 这里按照一个宏块或宏块对（当为MBAFF时，遍历大小减小一半）处理 */
    for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++)
      /* 确保在只有一个Slice组的情况下，整个图像的所有宏块都被正确地映射到这个唯一的Slice组上，简化处理逻辑这里赋值不一定非要为0,只要保持映射单元内都是同一个值就行了 */
      mapUnitToSliceGroupMap[i] = 0;
    /* TODO YangJing 有问题，如果是场编码，那么这里实际上只处理了一半映射 <24-09-16 00:07:20> */
    return 0;
  }

  /* TODO YangJing 这里还没测过，怎么造一个多Slice文件？ <24-09-15 23:25:12> */

  /* — 否则（num_slice_groups_minus1 不等于0），mapUnitToSliceGroupMap 的推导如下： 
       * — 如果slice_group_map_type 等于0，则应用第8.2.2.1 节中指定的mapUnitToSliceGroupMap 的推导。  
       * — 否则，如果slice_group_map_type等于1，则应用第8.2.2.2节中指定的mapUnitToSliceGroupMap的推导。  
       * — 否则，如果slice_group_map_type等于2，则应用第8.2.2.3节中指定的mapUnitToSliceGroupMap的推导。
       * – 否则，如果slice_group_map_type等于3，则应用第8.2.2.4节中指定的mapUnitToSliceGroupMap的推导。  
       * — 否则，如果slice_group_map_type等于4，则应用第8.2.2.5节中指定的mapUnitToSliceGroupMap的推导。  
       * – 否则，如果slice_group_map_type等于5，则应用第8.2.2.6节中指定的mapUnitToSliceGroupMap的推导。  
       * — 否则（slice_group_map_type 等于 6），应用第 8.2.2.7 节中指定的 mapUnitToSliceGroupMap 的推导。*/

  switch (m_pps->slice_group_map_type) {
  case 0:
    interleaved_slice_group_map_type(mapUnitToSliceGroupMap);
    break;
  case 1:
    dispersed_slice_group_map_type(mapUnitToSliceGroupMap);
    break;
  case 2:
    foreground_with_left_over_slice_group_ma_type(mapUnitToSliceGroupMap);
    break;
  case 3:
    box_out_slice_group_map_types(mapUnitToSliceGroupMap,
                                  MapUnitsInSliceGroup0);
    break;
  case 4:
    raster_scan_slice_group_map_types(mapUnitToSliceGroupMap,
                                      MapUnitsInSliceGroup0);
    break;
  case 5:
    wipe_slice_group_map_types(mapUnitToSliceGroupMap, MapUnitsInSliceGroup0);
    break;
  default:
    explicit_slice_group_map_type(mapUnitToSliceGroupMap);
    break;
  }
  return 0;
}

// 8.2.2.8 Specification for conversion of map unit to slice group map to macroblock to slice group map
/* 宏块（Macroblock）的位置映射到Slice（Slice）的过程*/
inline int SliceData::mbToSliceGroupMap() {
  /* 输入：存储每个宏块单元对应的Slice Group索引，在A Frame = A Slice的情况下，这里均为0 */
  const int32_t *mapUnitToSliceGroupMap = header->mapUnitToSliceGroupMap;

  /* 输出：存储映射后每个宏块对应的Slice Group索引 */
  int32_t *&MbToSliceGroupMap = header->MbToSliceGroupMap;

  /* 对于Slice中的每个宏块（若是场编码，顶、底宏块也需要单独遍历），宏块到Slice Group映射指定如下： */
  for (int mbIndex = 0; mbIndex < header->PicSizeInMbs; mbIndex++) {
    if (m_sps->frame_mbs_only_flag || header->field_pic_flag)
      /* 对于一个全帧或全场（即不存在混合帧、场），每个宏块独立对应一个映射单位 */
      MbToSliceGroupMap[mbIndex] = mapUnitToSliceGroupMap[mbIndex];
    else if (MbaffFrameFlag)
      /* 映射基于宏块对，一个宏块对（2个宏块）共享一个映射单位 */
      MbToSliceGroupMap[mbIndex] = mapUnitToSliceGroupMap[mbIndex / 2];
    else {
      /* 场编码的交错模式，每个宏块对跨越两行，每一对宏块（通常包括顶部场和底部场的宏块）被当作一个单元处理 */
      /*   +-----------+        +-----------+
           |           |        | top filed |
           |           |        | btm filed |
           | A  Slice  |   -->  | top filed | 
           |           |        | btm filed |
           |           |        | top filed |
           |           |        | btm filed |
           +-----------+        +-----------+*/
      /* 用宏块索引对图像宽度取模，得到该宏块在其行中的列位置 */
      uint32_t x = mbIndex % m_sps->PicWidthInMbs;
      /* 因为每个宏块对占据两行（每行一个宏块），所以用宽度的两倍来计算行号，得到顶宏块的行数 */
      uint32_t y = mbIndex / (2 * m_sps->PicWidthInMbs);
      MbToSliceGroupMap[mbIndex] =
          mapUnitToSliceGroupMap[y * m_sps->PicWidthInMbs + x];
    }
  }

  return 0;
}

/* mb_skip_run指定连续跳过的宏块的数量，在解码P或SPSlice时，MB_type应被推断为P_Skip并且宏块类型统称为P宏块类型，或者在解码BSlice时， MB_type应被推断为B_Skip，并且宏块类型统称为B宏块类型 */
int SliceData::process_mb_skip_run(int32_t &prevMbSkipped) {
  mb_skip_run = bs->readUE();

  /* 当mb_skip_run > 0时，实际上在接下来的宏块解析中，将要跳过处理，所以能够表示prevMbSkipped = 1*/
  prevMbSkipped = mb_skip_run > 0;

  /* 对于每个跳过的宏块，不同于CABAC的处理，这里是处理多个宏块，而CABAC是处理单个宏块 */
  for (uint32_t i = 0; i < mb_skip_run; i++) {
    /* 1. 计算当前宏块的位置 */
    updatesLocationOfCurrentMacroblock(MbaffFrameFlag);

    /* 2. 对于跳过的宏块同样需要增加实际的宏块计数 */
    pic->mb_cnt++;

    /* 3. 对于MBAFF模式下，顶宏块需要先确定当前是否为帧宏块还是场宏块，以确定下面的帧间预测中如何处理宏块对 */
    if (MbaffFrameFlag && CurrMbAddr % 2 == 0)
      //这里是推导（根据前面已解码的宏块进行推导当前值），而不是解码
      derivation_for_mb_field_decoding_flag();

    /* 4. 宏块层也需要将该宏块该宏块的类型设置“Skip类型” */
    pic->m_mbs[pic->CurrMbAddr].decode_skip(*pic, *this, *cabac);

    /* 5. 由于跳过的宏块只能是P,B Slice中的宏块，那么就只需要调用帧间预测 */
    pic->inter_prediction_process();

    /* 6. 外层循环也需要更新宏块跳过地址 */
    CurrMbAddr = NextMbAddress(CurrMbAddr, header);
  }
  return 0;
}

/* 更新当前宏快位置 */
/* 输入：PicWidthInMbs,MbaffFrameFlag
 * 输出：mb_x, mb_y, CurrMbAddr*/
void SliceData::updatesLocationOfCurrentMacroblock(const bool MbaffFrameFlag) {
  const uint32_t h = MbaffFrameFlag ? 2 : 1;
  const uint32_t w = pic->PicWidthInMbs * h; //当为宏块对模式时，实际上$W = 2*W$
  pic->mb_x = (CurrMbAddr % w) / h;
  pic->mb_y = (CurrMbAddr / w * h) + ((CurrMbAddr % w) % h);
  pic->CurrMbAddr = CurrMbAddr;
}

/* 如果当前宏块的运动矢量与参考帧中的预测块非常接近，且残差（即当前块与预测块的差异）非常小或为零，编码器可能会选择跳过该宏块的编码。
 * 在这种情况下，解码器可以通过运动矢量预测和参考帧直接重建宏块，而无需传输额外的残差信息。
 * 一般来说，在I帧中，不会出现宏块跳过处理。这是因为I帧中的宏块是使用帧内预测进行编码的，而不是基于参考帧的帧间预测 */
int SliceData::process_mb_skip_flag(const int32_t prevMbSkipped) {
  /* 1. 计算当前宏块的位置 */
  updatesLocationOfCurrentMacroblock(MbaffFrameFlag);

  MacroBlock &curr_mb = pic->m_mbs[CurrMbAddr];
  MacroBlock &next_mb = pic->m_mbs[CurrMbAddr + 1];

  /* 2. 设置当前宏块的Slice编号 */
  curr_mb.slice_number = slice_number;

  /* 3. 当前帧是MBAFF帧，且顶宏块和底宏块的场解码标志都未设置，则推导出mb_field_decoding_flag的值 */
  if (MbaffFrameFlag) {
    /* 当 mb_field_decoding_flag 未设置，需要推导其初始值，根据相邻“宏块对”来推导当前宏块类型，若为首个宏块(0,0)则设置为帧宏块。（不能理解为"当宏块对作为帧宏块处理时"）*/
    if (CurrMbAddr % 2 == 0 &&
        (!curr_mb.mb_field_decoding_flag && !next_mb.mb_field_decoding_flag))
      derivation_for_mb_field_decoding_flag();

    curr_mb.mb_field_decoding_flag = mb_field_decoding_flag;
  }

  /* 4. 在当前宏块对中，若首宏块跳过处理，则次宏块跳过标记设置为下个宏块对的首宏块一致。NOTE:这里是CABAC的一个特殊规则，为了减少比特流中需要传输的标志位数量 */
  if (MbaffFrameFlag && CurrMbAddr % 2 == 1 && prevMbSkipped)
    mb_skip_flag = mb_skip_flag_next_mb;

  /* 若非MBAFF模式，则直接解码获取是否需要跳过该宏块*/
  else
    cabac->decode_mb_skip_flag(CurrMbAddr, mb_skip_flag);

  /* 5. 宏块需要进行跳过处理时：由于宏块跳过时，一般来说没有残差数据，则需要运动矢量预测和参考帧直接重建宏块（帧间预测,P,B Slice） */
  if (mb_skip_flag && header->slice_type != SLICE_I &&
      header->slice_type != SLICE_SI) {
    pic->mb_cnt++;

    /* 若为MBAFF模式，则宏块对需要一并处理 */
    if (MbaffFrameFlag && CurrMbAddr % 2 == 0) {
      curr_mb.mb_skip_flag = mb_skip_flag;

      // 次宏块与首宏块的宏块对是相同的（要么都是帧宏块，要么都是场宏块），进行宏块对的同步
      next_mb.slice_number = slice_number;
      next_mb.mb_field_decoding_flag = mb_field_decoding_flag;

      /* 解码次宏块是否需要跳过宏块处理（由于宏块对是一起操作的，所以这里允许先解码下一个宏块）*/
      cabac->decode_mb_skip_flag(CurrMbAddr + 1, mb_skip_flag_next_mb);

      /* 当首宏块跳过处理，但次宏块需要解码 */
      if (mb_skip_flag_next_mb == 0) {
        cabac->decode_mb_field_decoding_flag(mb_field_decoding_flag);
        is_mb_field_decoding_flag_prcessed = true;
      }
      /* 当首、次宏块均跳过处理时，需根据标准规定，要再次推导当前首宏块的宏块对解码类型，确保宏块对类型正确（与上面一次调用不同的是，CABAC解码需要根据最新的上下文重新推导
       * NOTE: 这里有一个情况是当一个Slice被分为奇数行宏块时，那么在MBAFF模式下，宏块对的划分后，最后一行宏块会被当作首宏块处理，且需要与一个虚拟的底宏块组成宏块对进行处理，虚拟的底宏块会被视为跳过的宏块，不包含任何数据。 */
      else
        derivation_for_mb_field_decoding_flag();
    }

    /* 宏块层对应的处理 */
    curr_mb.decode_skip(*pic, *this, *cabac);

    /* 由于跳过的宏块只能是P,B Slice中的宏块，那么就只需要调用帧间预测 */
    pic->inter_prediction_process(); // 帧间预测
  }

  return 0;
}

//解码mb_field_decoding_flag: 表示本宏块对是帧宏块对，还是场宏块对
int SliceData::process_mb_field_decoding_flag(bool entropy_coding_mode_flag) {
  int ret = 0;
  if (is_mb_field_decoding_flag_prcessed) {
    is_mb_field_decoding_flag_prcessed = false;
    return 1;
  }

  if (entropy_coding_mode_flag)
    ret = cabac->decode_mb_field_decoding_flag(mb_field_decoding_flag);
  else
    mb_field_decoding_flag = bs->readU1();

  RET(ret);
  return 0;
}

int SliceData::process_end_of_slice_flag(int32_t &end_of_slice_flag) {
  cabac->decode_end_of_slice_flag(end_of_slice_flag);
  return 0;
}

/* mb_field_decoding_flag. ->  Rec. ITU-T H.264 (08/2021) 98*/
//该函数只适用于MBAFF模式下调用，根据相邻“宏块对”的状态来推断当前“宏块对”的 mb_field_decoding_flag 值
int SliceData::derivation_for_mb_field_decoding_flag() {
  /* NOTE:在同一Slice内，编码模式只能是一种，若存在左邻宏块或上邻宏块则编码模式必然一致（只能通过左、上方向推导、这是扫描顺序决定的） */
  /* 同一片中当前宏块对的左侧或上方不存在相邻宏块对，即默认为帧宏块 */
  mb_field_decoding_flag = 0;
  if (pic->mb_x > 0) {
    /* 存在左侧的相邻宏块对，且属于同一Slice group，则直接copy */
    auto &left_mb = pic->m_mbs[CurrMbAddr - 2];
    if (left_mb.slice_number == slice_number)
      mb_field_decoding_flag = left_mb.mb_field_decoding_flag;
  } else if (pic->mb_y > 0) {
    /* 存在上侧的相邻宏块对，且属于同一Slice group，则直接copy */
    auto &top_mb = pic->m_mbs[CurrMbAddr - 2 * pic->PicWidthInMbs];
    if (top_mb.slice_number == slice_number)
      mb_field_decoding_flag = top_mb.mb_field_decoding_flag;
  }
  return 0;
}

int SliceData::do_macroblock_layer() {
  /* 1. 计算当前宏块的位置 */
  updatesLocationOfCurrentMacroblock(MbaffFrameFlag);
  /* 2. 在宏块层中对每个宏块处理或解码得到对应帧内、帧间解码所需要的信息*/
  pic->m_mbs[pic->CurrMbAddr].decode(*bs, *pic, *this, *cabac);
  pic->mb_cnt++;
  return 0;
}

int SliceData::decoding_process() {
  /* ------------------ 设置别名 ------------------ */
  const int32_t &picWidthInSamplesL = pic->PicWidthInSamplesL;
  const int32_t &picWidthInSamplesC = pic->PicWidthInSamplesC;
  const int32_t &BitDepth = pic->m_slice->slice_header->m_sps->BitDepthY;

  uint8_t *&pic_buff_luma = pic->m_pic_buff_luma;
  uint8_t *&pic_buff_cb = pic->m_pic_buff_cb;
  uint8_t *&pic_buff_cr = pic->m_pic_buff_cr;

  MacroBlock &mb = pic->m_mbs[pic->CurrMbAddr];
  /* ------------------  End ------------------ */

  //8.5 Transform coefficient decoding process and picture construction process prior to deblocking filter process（根据不同类型的预测模式，进行去块滤波处理之前的变换系数解码处理和图片构造处理 ）
  bool isNeedIntraPrediction = true;
  //----------------------------------- 帧内预测 -----------------------------------
  if (mb.m_mb_pred_mode == Intra_4x4) //分区预测，处理最为复杂的高纹理区域
    pic->transform_decoding_for_4x4_luma_residual_blocks(
        0, 0, BitDepth, picWidthInSamplesL, pic_buff_luma);
  else if (mb.m_mb_pred_mode == Intra_8x8) //分区预测
    pic->transform_decoding_for_8x8_luma_residual_blocks(
        0, 0, BitDepth, picWidthInSamplesL, mb.LumaLevel8x8, pic_buff_luma);
  else if (mb.m_mb_pred_mode == Intra_16x16) //整块预测，处理较为简单的区域
    pic->transform_decoding_for_luma_samples_of_16x16(
        0, BitDepth, mb.QP1Y, picWidthInSamplesL, mb.Intra16x16DCLevel,
        mb.Intra16x16ACLevel, pic_buff_luma);
  //----------------------------------- 原始数据 -----------------------------------
  else if (mb.m_name_of_mb_type == I_PCM) {
    pic->sample_construction_for_I_PCM();
    goto eof;
  }
  //----------------------------------- 帧间预测 -----------------------------------
  else {
    // 对于帧间预测而言，过程中不需要调用帧内预测
    isNeedIntraPrediction = false;
    pic->inter_prediction_process();
    /* 选择 4x4 或 8x8 的残差块解码函数来处理亮度残差块 */
    if (mb.transform_size_8x8_flag)
      pic->transform_decoding_for_8x8_luma_residual_blocks(
          0, 0, BitDepth, picWidthInSamplesL, mb.LumaLevel8x8, pic_buff_luma,
          false);
    else
      pic->transform_decoding_for_4x4_luma_residual_blocks(
          0, 0, BitDepth, picWidthInSamplesL, pic_buff_luma, false);
  }

  /* 帧内、帧间预测都调用，当存在色度采样时，即YUV420,YUV422,YUV444进行色度解码; 反之，则不进行色度解码 */
  if (m_sps->ChromaArrayType) {
    pic->transform_decoding_for_chroma_samples(
        1, picWidthInSamplesC, pic_buff_cb, isNeedIntraPrediction);
    pic->transform_decoding_for_chroma_samples(
        0, picWidthInSamplesC, pic_buff_cr, isNeedIntraPrediction);
  }

  /* 至此该宏块的原始数据完成全部解码工作，输出的pic_buff_luma,pic_buff_cb,pic_buff_cr即为解码的原始数据 */
eof:
  return 0;
}

// 第 8.2.2.8 节的规定导出宏块到Slice Group映射后，该函数导出NextMbAddress的值
/* 跳过不属于当前Slice Group的宏块，找到与当前宏块位于同一Slice Group中的下一个宏块 */
int NextMbAddress(int currMbAddr, SliceHeader *header) {
  int nextMbAddr = currMbAddr + 1;

  /* 宏块索引不应该为负数或大于该Slice的总宏块数 + 1 */
  while (nextMbAddr < header->PicSizeInMbs &&
         header->MbToSliceGroupMap[nextMbAddr] !=
             header->MbToSliceGroupMap[currMbAddr]) {
    /* 下一个宏块是否与当前宏块位于同一个Slice Group中。如果不在同一个Slice Group中，则继续增加 nextMbAddr 直到找到属于同一个Slice Group的宏块。*/
    nextMbAddr++;
  }

  if (nextMbAddr < 0)
    cerr << "An error occurred CurrMbAddr:" << nextMbAddr << " on "
         << __FUNCTION__ << "():" << __LINE__ << endl;
  return nextMbAddr;
}

void SliceData::printFrameReorderPriorityInfo() {
  string sliceType = "UNKNOWN";
  cout << "\tGOP[" << pic->m_PicNumCnt + 1 << "] -> {" << endl;
  for (int i = 0; i < MAX_DPB; ++i) {
    const auto &refPic = pic->m_dpb[i];
    if (refPic) {
      auto &frame = refPic->m_picture_frame;
      auto &sliceHeader = frame.m_slice->slice_header;
      if (frame.m_slice == nullptr) continue;
      if (sliceHeader->slice_type != SLICE_I && frame.PicOrderCnt == 0 &&
          frame.PicNum == 0 && frame.m_PicNumCnt == 0)
        continue;
      sliceType = H264_SLIECE_TYPE_TO_STR(sliceHeader->slice_type);
      if (pic->PicOrderCnt == frame.PicOrderCnt)
        cout << "\t\t* DPB[" << i << "]: ";
      else
        cout << "\t\t  DPB[" << i << "]: ";
      cout << sliceType << "; POC(显示顺序)=" << frame.PicOrderCnt
           << "; frame_num(帧编号，编码顺序)="
           << frame.m_slice->slice_header->frame_num << ";\n";
    }
  }
  cout << "\t}" << endl;

  if (header->slice_type == SLICE_P || header->slice_type == SLICE_SP)
    cout << "\t当前帧所参考帧列表(按frame_num排序) -> {" << endl;
  else if (header->slice_type == SLICE_B)
    cout << "\t当前帧所参考帧列表(按POC排序) -> {" << endl;

  for (uint32_t i = 0; i < pic->m_RefPicList0Length; ++i) {
    const auto &refPic = pic->m_RefPicList0[i];
    if (refPic && (refPic->reference_marked_type == SHORT_REF ||
                   refPic->reference_marked_type == LONG_REF)) {
      auto &frame = refPic->m_picture_frame;
      auto &sliceHeader = frame.m_slice->slice_header;

      sliceType = H264_SLIECE_TYPE_TO_STR(sliceHeader->slice_type);
      cout << "\t\t(前参考)RefPicList0[" << i << "]: " << sliceType
           << "; POC(显示顺序)=" << frame.PicOrderCnt
           << "; frame_num(帧编号，编码顺序)=" << frame.PicNum << ";\n";
    }
  }

  for (uint32_t i = 0; i < pic->m_RefPicList1Length; ++i) {
    const auto &refPic = pic->m_RefPicList1[i];
    if (refPic && (refPic->reference_marked_type == SHORT_REF ||
                   refPic->reference_marked_type == LONG_REF)) {
      auto &frame = refPic->m_picture_frame;
      auto &sliceHeader = frame.m_slice->slice_header;

      sliceType = H264_SLIECE_TYPE_TO_STR(sliceHeader->slice_type);
      cout << "\t\t(后参考)RefPicList1[" << i << "]: " << sliceType
           << "; POC(显示顺序)=" << frame.PicOrderCnt
           << "; frame_num(帧编号，编码顺序)=" << frame.PicNum << ";\n";
    }
  }
  cout << "\t}" << endl;
}

//8.2.2.1 Specification for interleaved slice group map type
int SliceData::interleaved_slice_group_map_type(
    int32_t *&mapUnitToSliceGroupMap) {
  uint32_t i = 0;
  do {
    for (uint32_t iGroup = 0; iGroup <= m_pps->num_slice_groups_minus1 &&
                              i < m_sps->PicSizeInMapUnits;
         i += m_pps->run_length_minus1[iGroup++] + 1) {
      for (uint32_t j = 0; j <= m_pps->run_length_minus1[iGroup] &&
                           i + j < m_sps->PicSizeInMapUnits;
           j++) {
        mapUnitToSliceGroupMap[i + j] = iGroup;
      }
    }
  } while (i < m_sps->PicSizeInMapUnits);
  return 0;
}
//8.2.2.2 Specification for dispersed slice group map type
int SliceData::dispersed_slice_group_map_type(
    int32_t *&mapUnitToSliceGroupMap) {
  for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++) {
    mapUnitToSliceGroupMap[i] =
        ((i % m_sps->PicWidthInMbs) +
         (((i / m_sps->PicWidthInMbs) * (m_pps->num_slice_groups_minus1 + 1)) /
          2)) %
        (m_pps->num_slice_groups_minus1 + 1);
  }
  return 0;
}
//8.2.2.3 Specification for foreground with left-over slice group map type
int SliceData::foreground_with_left_over_slice_group_ma_type(
    int32_t *&mapUnitToSliceGroupMap) {
  for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++)
    mapUnitToSliceGroupMap[i] = m_pps->num_slice_groups_minus1;

  for (int iGroup = m_pps->num_slice_groups_minus1 - 1; iGroup >= 0; iGroup--) {
    int32_t yTopLeft = m_pps->top_left[iGroup] / m_sps->PicWidthInMbs;
    int32_t xTopLeft = m_pps->top_left[iGroup] % m_sps->PicWidthInMbs;
    int32_t yBottomRight = m_pps->bottom_right[iGroup] / m_sps->PicWidthInMbs;
    int32_t xBottomRight = m_pps->bottom_right[iGroup] % m_sps->PicWidthInMbs;
    for (int y = yTopLeft; y <= yBottomRight; y++) {
      for (int x = xTopLeft; x <= xBottomRight; x++) {
        mapUnitToSliceGroupMap[y * m_sps->PicWidthInMbs + x] = iGroup;
      }
    }
  }
  return 0;
}

//8.2.2.4 Specification for box-out slice group map types
int SliceData::box_out_slice_group_map_types(int32_t *&mapUnitToSliceGroupMap,
                                             const int &MapUnitsInSliceGroup0) {

  for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++)
    mapUnitToSliceGroupMap[i] = 1;

  int x = (m_sps->PicWidthInMbs - m_pps->slice_group_change_direction_flag) / 2;
  int y =
      (m_sps->PicHeightInMapUnits - m_pps->slice_group_change_direction_flag) /
      2;

  int32_t leftBound = x;
  int32_t topBound = y;
  int32_t rightBound = x;
  int32_t bottomBound = y;
  int32_t xDir = m_pps->slice_group_change_direction_flag - 1;
  int32_t yDir = m_pps->slice_group_change_direction_flag;
  int32_t mapUnitVacant = 0;

  for (int k = 0; k < MapUnitsInSliceGroup0; k += mapUnitVacant) {
    mapUnitVacant = (mapUnitToSliceGroupMap[y * m_sps->PicWidthInMbs + x] == 1);
    if (mapUnitVacant) {
      mapUnitToSliceGroupMap[y * m_sps->PicWidthInMbs + x] = 0;
    }
    if (xDir == -1 && x == leftBound) {
      leftBound = fmax(leftBound - 1, 0);
      x = leftBound;
      xDir = 0;
      yDir = 2 * m_pps->slice_group_change_direction_flag - 1;
    } else if (xDir == 1 && x == rightBound) {
      rightBound = MIN(rightBound + 1, m_sps->PicWidthInMbs - 1);
      x = rightBound;
      xDir = 0;
      yDir = 1 - 2 * m_pps->slice_group_change_direction_flag;
    } else if (yDir == -1 && y == topBound) {
      topBound = MAX(topBound - 1, 0);
      y = topBound;
      xDir = 1 - 2 * m_pps->slice_group_change_direction_flag;
      yDir = 0;
    } else if (yDir == 1 && y == bottomBound) {
      bottomBound = MIN(bottomBound + 1, m_sps->PicHeightInMapUnits - 1);
      y = bottomBound;
      xDir = 2 * m_pps->slice_group_change_direction_flag - 1;
      yDir = 0;
    } else {
      //(x, y) = (x + xDir, y + yDir);
    }
  }
  return 0;
}
//8.2.2.5 Specification for raster scan slice group map types
int SliceData::raster_scan_slice_group_map_types(
    int32_t *&mapUnitToSliceGroupMap, const int &MapUnitsInSliceGroup0) {
  // 8.2.2.5 Specification for raster scan slice group map types
  // 栅格扫描型 slice 组映射类型的描述
  int32_t sizeOfUpperLeftGroup = 0;
  if (m_pps->num_slice_groups_minus1 == 1) {
    sizeOfUpperLeftGroup =
        (m_pps->slice_group_change_direction_flag
             ? (m_sps->PicSizeInMapUnits - MapUnitsInSliceGroup0)
             : MapUnitsInSliceGroup0);
  }

  for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++) {
    if (i < sizeOfUpperLeftGroup)
      mapUnitToSliceGroupMap[i] = m_pps->slice_group_change_direction_flag;
    else
      mapUnitToSliceGroupMap[i] = 1 - m_pps->slice_group_change_direction_flag;
  }
  return 0;
}
//8.2.2.6 Specification for wipe slice group map types
int SliceData::wipe_slice_group_map_types(int32_t *&mapUnitToSliceGroupMap,
                                          const int &MapUnitsInSliceGroup0) {
  int32_t sizeOfUpperLeftGroup = 0;
  if (m_pps->num_slice_groups_minus1 == 1) {
    sizeOfUpperLeftGroup =
        (m_pps->slice_group_change_direction_flag
             ? (m_sps->PicSizeInMapUnits - MapUnitsInSliceGroup0)
             : MapUnitsInSliceGroup0);
  }

  int k = 0;
  for (int j = 0; j < (int)m_sps->PicWidthInMbs; j++) {
    for (int i = 0; i < (int)m_sps->PicHeightInMapUnits; i++) {
      if (k++ < sizeOfUpperLeftGroup) {
        mapUnitToSliceGroupMap[i * m_sps->PicWidthInMbs + j] =
            m_pps->slice_group_change_direction_flag;
      } else {
        mapUnitToSliceGroupMap[i * m_sps->PicWidthInMbs + j] =
            1 - m_pps->slice_group_change_direction_flag;
      }
    }
  }
  return 0;
}

//8.2.2.7 Specification for explicit slice group map type
int SliceData::explicit_slice_group_map_type(int32_t *&mapUnitToSliceGroupMap) {
  for (int i = 0; i < (int)m_sps->PicSizeInMapUnits; i++)
    mapUnitToSliceGroupMap[i] = m_pps->slice_group_id[i];
  return 0;
}

//6.5.3 Up-right diagonal scan order array initialization process
//该过程的输入是块大小 blkSize。
//该过程的输出是数组 diagScan[ sPos ][ sComp ]。数组索引 sPos 指定扫描位置，范围从 0 到 ( blkSize * blkSize ) − 1。数组索引 sComp 等于 0 指定水平分量，数组索引 sComp 等于 1 指定垂直分量。根据 blkSize 的值，数组 diagScan 的推导如下：
int SliceData::Up_right_diagonal_scan_order_array_initialization_process(
    int blkSize, uint8_t diagScan[16][2]) {
  static const uint8_t diag2x2_x[4] = {0, 0, 1, 1};
  static const uint8_t diag2x2_y[4] = {0, 1, 0, 1};
  static const uint8_t diag4x4_x[16] = {
      0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 1, 2, 3, 2, 3, 3,
  };
  static const uint8_t diag4x4_y[16] = {
      0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 3, 2, 3,
  };
  static const uint8_t diag8x8_x[64] = {
      0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 3, 4, 0,
      1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3,
      4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7, 2, 3, 4, 5, 6,
      7, 3, 4, 5, 6, 7, 4, 5, 6, 7, 5, 6, 7, 6, 7, 7,
  };
  static const uint8_t diag8x8_y[64] = {
      0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 4, 3, 2, 1, 0, 5,
      4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4,
      3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3,
      2, 7, 6, 5, 4, 3, 7, 6, 5, 4, 7, 6, 5, 7, 6, 7,
  };

  if (blkSize == 1) {
    diagScan[0][0] = 0;
    diagScan[0][1] = 0;
  } else if (blkSize == 2) {
    for (int i = 0; i < 4; ++i) {
      diagScan[i][0] = diag2x2_x[i];
      diagScan[i][1] = diag2x2_y[i];
    }
  } else if (blkSize == 4) {
    for (int i = 0; i < 16; ++i) {
      diagScan[i][0] = diag4x4_x[i];
      diagScan[i][1] = diag4x4_y[i];
    }
  } else if (blkSize == 8) {
    for (int i = 0; i < 64; ++i) {
      diagScan[i][0] = diag8x8_x[i];
      diagScan[i][1] = diag8x8_y[i];
    }
  }
  return 0;
}

//6.5.4 Horizontal scan order array initialization process
//Input to this process is a block size blkSize.
//Output of this process is the array horScan[ sPos ][ sComp ]. The array index sPos specifies the scan position ranging from 0 to ( blkSize * blkSize ) − 1. The array index sComp equal to 0 specifies the horizontal component and the array index sComp equal to 1 specifies the vertical component. Depending on the value of blkSize, the array horScan is derived as follows:
int SliceData::Horizontal_scan_order_array_initialization_process(
    int blkSize, uint8_t horScan[16][2]) {
  int i = 0;
  for (int y = 0; y < blkSize; y++)
    for (int x = 0; x < blkSize; x++) {
      horScan[i][0] = x;
      horScan[i][1] = y;
      i++;
    }
  return 0;
}

//6.5.5 Vertical scan order array initialization process
//Input to this process is a block size blkSize.
//Output of this process is the array verScan[ sPos ][ sComp ]. The array index sPos specifies the scan position ranging from 0 to ( blkSize * blkSize ) − 1. The array index sComp equal to 0 specifies the horizontal component and the array index sComp equal to 1 specifies the vertical component. Depending on the value of blkSize, the array verScan is derived as follows:
int SliceData::Vertical_scan_order_array_initialization_process(
    int blkSize, uint8_t verScan[16][2]) {
  int i = 0;
  for (int x = 0; x < blkSize; x++)
    for (int y = 0; y < blkSize; y++) {
      verScan[i][0] = x;
      verScan[i][1] = y;
      i++;
    }
  return 0;
}

//6.5.6 Traverse scan order array initialization process
//Input to this process is a block size blkSize.
//Output of this process is the array travScan[ sPos ][ sComp ]. The array index sPos specifies the scan position ranging from 0 to ( blkSize * blkSize ) − 1, inclusive. The array index sComp equal to 0 specifies the horizontal component and the array index sComp equal to 1 specifies the vertical component. Depending on the value of blkSize, the array travScan is derived as follows:
int SliceData::Traverse_scan_order_array_initialization_process(
    int blkSize, uint8_t travScan[16][2]) {
  int i = 0;
  for (int y = 0; y < blkSize; y++)
    if (y % 2 == 0)
      for (int x = 0; x < blkSize; x++) {
        travScan[i][0] = x;
        travScan[i][1] = y;
        i++;
      }
    else
      for (int x = blkSize - 1; x >= 0; x--) {
        travScan[i][0] = x;
        travScan[i][1] = y;
        i++;
      }
  return 0;
}

int SliceData::get_scf_offse0(int &scf_offset, uint8_t *&ctx_idx_map_p,
                              int transform_skip_flag, int cIdx,
                              int log2TrafoSize, int x_cg, int y_cg,
                              int scan_idx, int prev_sig, int i) {
  if (m_sps->transform_skip_context_enabled_flag &&
      (transform_skip_flag || cu_transquant_bypass_flag)) {
    if (cIdx == 0) {
      scf_offset = 42;
    } else {
      scf_offset = 16 + 27;
    }
  } else {
    if (i == 0) {
      if (cIdx == 0)
        scf_offset = 0;
      else
        scf_offset = 27;
    } else {
      scf_offset = 2 + scf_offset;
    }
  }
  return 0;
}

int SliceData::get_scf_offse(int &scf_offset, uint8_t *&ctx_idx_map_p,
                             int transform_skip_flag, int cIdx,
                             int log2TrafoSize, int x_cg, int y_cg,
                             int scan_idx, int prev_sig) {
  static const uint8_t ctx_idx_map[] = {
      0, 1, 4, 5, 2, 3, 4, 5, 6, 6, 8, 8, 7, 7, 8, 8, // log2_trafo_size == 2
      1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, // prev_sig == 0
      2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, // prev_sig == 1
      2, 1, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, // prev_sig == 2
      2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2  // default
  };
  if (m_sps->transform_skip_context_enabled_flag &&
      (transform_skip_flag || cu_transquant_bypass_flag)) {
    ctx_idx_map_p = (uint8_t *)&ctx_idx_map[4 * 16];
    if (cIdx == 0) {
      scf_offset = 40;
    } else {
      scf_offset = 14 + 27;
    }
  } else {
    if (cIdx != 0) scf_offset = 27;
    if (log2TrafoSize == 2) {
      ctx_idx_map_p = (uint8_t *)&ctx_idx_map[0];
    } else {
      ctx_idx_map_p = (uint8_t *)&ctx_idx_map[(prev_sig + 1) << 4];
      if (cIdx == 0) {
        if ((x_cg > 0 || y_cg > 0)) scf_offset += 3;
        if (log2TrafoSize == 3) {
          scf_offset += (scan_idx == SCAN_DIAG) ? 9 : 15;
        } else {
          scf_offset += 21;
        }
      } else {
        if (log2TrafoSize == 3)
          scf_offset += 9;
        else
          scf_offset += 12;
      }
    }
  }
  return 0;
}

int SliceData::luma_intra_pred_mode(int x0, int y0, int pu_size,
                                    int prev_intra_luma_pred_flag) {
  constexpr int kIntraPlanar = 0;
  constexpr int kIntraDc = 1;
  constexpr int kIntraAngular26 = 26;

  if (m_sps == nullptr || tab_ipm.empty()) return kIntraPlanar;

  const int log2_min_pu_size = m_sps->log2_min_luma_coding_block_size - 1;
  const int min_pu_width = m_sps->min_pu_width;
  const int min_pu_height = m_sps->min_pu_height;
  const int x_pu = x0 >> log2_min_pu_size;
  const int y_pu = y0 >> log2_min_pu_size;
  int size_in_pus = pu_size >> log2_min_pu_size;
  if (size_in_pus <= 0) size_in_pus = 1;

  const int x0b = av_mod_uintp2_c(x0, m_sps->CtbLog2SizeY);
  const int y0b = av_mod_uintp2_c(y0, m_sps->CtbLog2SizeY);

  int cand_up = kIntraDc;
  if ((ctb_up_flag || y0b) && y_pu > 0 && x_pu >= 0 && x_pu < min_pu_width) {
    cand_up = tab_ipm[(y_pu - 1) * min_pu_width + x_pu];
  }

  int cand_left = kIntraDc;
  if ((ctb_left_flag || x0b) && x_pu > 0 && y_pu >= 0 && y_pu < min_pu_height) {
    cand_left = tab_ipm[y_pu * min_pu_width + x_pu - 1];
  }

  const int y_ctb = (y0 >> m_sps->CtbLog2SizeY) << m_sps->CtbLog2SizeY;
  if ((y0 - 1) < y_ctb) cand_up = kIntraDc;

  int candidate[3] = {kIntraPlanar, kIntraDc, kIntraAngular26};
  if (cand_left == cand_up) {
    if (cand_left < 2) {
      candidate[0] = kIntraPlanar;
      candidate[1] = kIntraDc;
      candidate[2] = kIntraAngular26;
    } else {
      candidate[0] = cand_left;
      candidate[1] = 2 + ((cand_left - 2 - 1 + 32) & 31);
      candidate[2] = 2 + ((cand_left - 2 + 1) & 31);
    }
  } else {
    candidate[0] = cand_left;
    candidate[1] = cand_up;
    if (candidate[0] != kIntraPlanar && candidate[1] != kIntraPlanar) {
      candidate[2] = kIntraPlanar;
    } else if (candidate[0] != kIntraDc && candidate[1] != kIntraDc) {
      candidate[2] = kIntraDc;
    } else {
      candidate[2] = kIntraAngular26;
    }
  }

  int intra_pred_mode = kIntraPlanar;
  if (prev_intra_luma_pred_flag) {
    const int mpm_idx = Clip3(0, 2, m_pu.mpm_idx);
    intra_pred_mode = candidate[mpm_idx];
  } else {
    if (candidate[0] > candidate[1]) std::swap(candidate[0], candidate[1]);
    if (candidate[0] > candidate[2]) std::swap(candidate[0], candidate[2]);
    if (candidate[1] > candidate[2]) std::swap(candidate[1], candidate[2]);

    intra_pred_mode = m_pu.rem_intra_luma_pred_mode;
    for (int i = 0; i < 3; ++i)
      if (intra_pred_mode >= candidate[i]) intra_pred_mode++;
  }

  for (int dy = 0; dy < size_in_pus; ++dy) {
    const int py = y_pu + dy;
    if (py < 0 || py >= min_pu_height) continue;
    for (int dx = 0; dx < size_in_pus; ++dx) {
      const int px = x_pu + dx;
      if (px < 0 || px >= min_pu_width) continue;
      tab_ipm[py * min_pu_width + px] = (uint8_t)intra_pred_mode;
    }
  }

  return intra_pred_mode;
#if 0
  //HEVCLocalContext *lc = s->HEVClc;
  int x_pu = x0 >> m_sps->log2_min_pu_size;
  int y_pu = y0 >> m_sps->log2_min_pu_size;
  int min_pu_width = m_sps->min_pu_width;
  int size_in_pus = pu_size >> m_sps->log2_min_pu_size;
  int x0b = av_mod_uintp2(x0, m_sps->CtbLog2SizeY);
  int y0b = av_mod_uintp2(y0, m_sps->CtbLog2SizeY);

  int cand_up = (ctb_up_flag || y0b)
                    ? s->tab_ipm[(y_pu - 1) * min_pu_width + x_pu]
                    : INTRA_DC;
  int cand_left = (lc->ctb_left_flag || x0b)
                      ? s->tab_ipm[y_pu * min_pu_width + x_pu - 1]
                      : INTRA_DC;

  int y_ctb = (y0 >> (m_sps->CtbLog2SizeY)) << (m_sps->CtbLog2SizeY);

  MvField *tab_mvf = s->ref->tab_mvf;
  int intra_pred_mode;
  int candidate[3];
  int i, j;

  // intra_pred_mode prediction does not cross vertical CTB boundaries
  if ((y0 - 1) < y_ctb) cand_up = INTRA_DC;

  if (cand_left == cand_up) {
    if (cand_left < 2) {
      candidate[0] = INTRA_PLANAR;
      candidate[1] = INTRA_DC;
      candidate[2] = INTRA_ANGULAR_26;
    } else {
      candidate[0] = cand_left;
      candidate[1] = 2 + ((cand_left - 2 - 1 + 32) & 31);
      candidate[2] = 2 + ((cand_left - 2 + 1) & 31);
    }
  } else {
    candidate[0] = cand_left;
    candidate[1] = cand_up;
    if (candidate[0] != INTRA_PLANAR && candidate[1] != INTRA_PLANAR) {
      candidate[2] = INTRA_PLANAR;
    } else if (candidate[0] != INTRA_DC && candidate[1] != INTRA_DC) {
      candidate[2] = INTRA_DC;
    } else {
      candidate[2] = INTRA_ANGULAR_26;
    }
  }

  if (prev_intra_luma_pred_flag) {
    intra_pred_mode = candidate[lc->pu.mpm_idx];
  } else {
    if (candidate[0] > candidate[1])
      FFSWAP(uint8_t, candidate[0], candidate[1]);
    if (candidate[0] > candidate[2])
      FFSWAP(uint8_t, candidate[0], candidate[2]);
    if (candidate[1] > candidate[2])
      FFSWAP(uint8_t, candidate[1], candidate[2]);

    intra_pred_mode = lc->pu.rem_intra_luma_pred_mode;
    for (i = 0; i < 3; i++)
      if (intra_pred_mode >= candidate[i]) intra_pred_mode++;
  }

  /* write the intra prediction units into the mv array */
  if (!size_in_pus) size_in_pus = 1;
  for (i = 0; i < size_in_pus; i++) {
    memset(&s->tab_ipm[(y_pu + i) * min_pu_width + x_pu], intra_pred_mode,
           size_in_pus);

    for (j = 0; j < size_in_pus; j++) {
      tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].pred_flag = PF_INTRA;
    }
  }

  return intra_pred_mode;
#endif
}
