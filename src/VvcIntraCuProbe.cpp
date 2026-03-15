#include "VvcIntraCuProbe.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>

namespace {

constexpr int kNumLumaMode = 67;
constexpr int kNumMostProbableModes = 6;
constexpr int kPlanarIdx = 0;
constexpr int kDcIdx = 1;
constexpr int kNumDir = (((kNumLumaMode - 3) >> 2) + 1);
constexpr int kHorIdx = (1 * (kNumDir - 1) + 2);
constexpr int kVerIdx = (3 * (kNumDir - 1) + 2);
constexpr int kMinTbSizeY = 4;
constexpr int kMultiRefLineIdx[4] = {0, 1, 2, 0};

template <size_t N>
void initContextArray(
    std::array<VvcCabacContextModel, N> &ctxs, int qp, int slice_type,
    const std::array<std::array<uint8_t, N>, 3> &init_table) {
  const int st = slice_type < 0 ? 0 : (slice_type > 2 ? 2 : slice_type);
  for (size_t i = 0; i < N; ++i) {
    ctxs[i].init(qp, init_table[st][i]);
  }
}

struct IntraCuContexts {
  std::array<VvcCabacContextModel, 4> mip_flag;
  std::array<VvcCabacContextModel, 2> multi_ref_line_idx;
  std::array<VvcCabacContextModel, 1> ipred_mode;
  std::array<VvcCabacContextModel, 2> intra_luma_planar_flag;
  std::array<VvcCabacContextModel, 2> isp_mode;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 4>, 3> kMipFlagInit = {{
        {{56, 57, 50, 26}},
        {{41, 57, 58, 26}},
        {{33, 49, 50, 25}},
    }};
    static const std::array<std::array<uint8_t, 2>, 3> kMultiRefLineIdxInit = {{
        {{25, 59}},
        {{25, 58}},
        {{25, 60}},
    }};
    static const std::array<std::array<uint8_t, 1>, 3> kIPredModeInit = {{
        {{44}},
        {{36}},
        {{45}},
    }};
    static const std::array<std::array<uint8_t, 2>, 3>
        kIntraLumaPlanarFlagInit = {{
            {{13, 6}},
            {{12, 20}},
            {{13, 28}},
        }};
    static const std::array<std::array<uint8_t, 2>, 3> kIspModeInit = {{
        {{33, 43}},
        {{33, 36}},
        {{33, 43}},
    }};

    initContextArray(mip_flag, qp, slice_type, kMipFlagInit);
    initContextArray(multi_ref_line_idx, qp, slice_type, kMultiRefLineIdxInit);
    initContextArray(ipred_mode, qp, slice_type, kIPredModeInit);
    initContextArray(intra_luma_planar_flag, qp, slice_type,
                     kIntraLumaPlanarFlagInit);
    initContextArray(isp_mode, qp, slice_type, kIspModeInit);
  }
};

int floorLog2(int value) {
  int log2 = -1;
  while (value > 0) {
    value >>= 1;
    ++log2;
  }
  return log2;
}

uint32_t decodeBypassBins(VvcCabacReader &cabac, int num_bins) {
  uint32_t value = 0;
  for (int i = 0; i < num_bins; ++i) {
    value = (value << 1) | static_cast<uint32_t>(cabac.decodeBypass() & 1);
  }
  return value;
}

uint32_t decodeTruncBinCode(VvcCabacReader &cabac, uint32_t max_symbol) {
  if (max_symbol <= 1) return 0;

  const int thresh = floorLog2(static_cast<int>(max_symbol));
  const uint32_t val = 1u << thresh;
  const uint32_t b = max_symbol - val;

  uint32_t symbol = decodeBypassBins(cabac, thresh);
  if (symbol >= val - b) {
    symbol = (symbol << 1) | static_cast<uint32_t>(cabac.decodeBypass() & 1);
    symbol -= (val - b);
  }
  return symbol;
}

int getNumModesMip(int width, int height) {
  if (width == 4 && height == 4) return 16;
  if (width == 4 || height == 4 || (width == 8 && height == 8)) return 8;
  return 6;
}

int deriveMipContextId(int width, int height) {
  if (width > 2 * height || height > 2 * width) return 3;
  return 0;
}

int deriveAllowedIspSplit(int width, int height, int max_tb_size) {
  const bool not_enough_samples_to_split =
      (floorLog2(width) + floorLog2(height)) <=
      (floorLog2(kMinTbSizeY) << 1);
  const bool cu_too_large = width > max_tb_size || height > max_tb_size;
  const int width_can_be_used =
      (!cu_too_large && !not_enough_samples_to_split) ? 4 : 2;
  const int height_can_be_used =
      (!cu_too_large && !not_enough_samples_to_split) ? 0 : 2;
  return width_can_be_used >> height_can_be_used;
}

void deriveFirstCuDefaultMpm(std::array<int, 6> &mpm) {
  mpm[0] = kPlanarIdx;
  mpm[1] = kDcIdx;
  mpm[2] = kVerIdx;
  mpm[3] = kHorIdx;
  mpm[4] = kVerIdx - 4;
  mpm[5] = kVerIdx + 4;
}

} // namespace

int VvcIntraCuProbe::probeFirstCuIntraSyntax(
    VvcCabacReader &cabac, const VvcSpsState &sps, int slice_type,
    int slice_qp_y, const VvcSplitProbeResult &split_result,
    VvcIntraCuProbeResult &result) {
  m_last_error.clear();
  result = VvcIntraCuProbeResult{};

  if (!sps.valid) {
    setError("VVC intra CU probe requires valid SPS state");
    return -1;
  }
  if (!split_result.valid || split_result.leaf_width <= 0 ||
      split_result.leaf_height <= 0) {
    setError("VVC intra CU probe requires valid split probe result");
    return -1;
  }
  if (slice_type != I_SLICE) {
    setError("VVC intra CU probe currently only supports I slices");
    return -1;
  }

  result.width = split_result.leaf_width;
  result.height = split_result.leaf_height;

  IntraCuContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  deriveFirstCuDefaultMpm(result.mpm_candidates);

  if (sps.mip_enabled_flag) {
    const int mip_ctx = deriveMipContextId(result.width, result.height);
    result.mip_flag = cabac.decodeBin(ctxs.mip_flag[mip_ctx]) != 0;
  }

  if (result.mip_flag) {
    result.mip_transposed_flag = cabac.decodeBypass() != 0;
    result.mip_mode =
        static_cast<int>(decodeTruncBinCode(cabac, getNumModesMip(
                                                       result.width,
                                                       result.height)));
    result.intra_luma_mode = result.mip_mode;
    {
      std::ostringstream oss;
      oss << "mip[" << result.mip_mode << "]";
      result.intra_luma_source = oss.str();
    }
    result.valid = true;
    return 0;
  }

  if (sps.mrl_enabled_flag) {
    const bool is_first_line_of_ctu = (result.y0 & (sps.ctu_size - 1)) == 0;
    if (!is_first_line_of_ctu) {
      result.multi_ref_idx =
          cabac.decodeBin(ctxs.multi_ref_line_idx[0]) != 0
              ? kMultiRefLineIdx[1]
              : kMultiRefLineIdx[0];
      if (result.multi_ref_idx != 0) {
        result.multi_ref_idx =
            cabac.decodeBin(ctxs.multi_ref_line_idx[1]) != 0
                ? kMultiRefLineIdx[2]
                : kMultiRefLineIdx[1];
      }
    }
  }

  if (sps.isp_enabled_flag && result.multi_ref_idx == 0) {
    const int allowed_isp =
        deriveAllowedIspSplit(result.width, result.height, sps.max_tb_size);
    if (allowed_isp != 0) {
      const int isp_flag = cabac.decodeBin(ctxs.isp_mode[0]);
      if (isp_flag != 0) {
        if (allowed_isp == 1 || allowed_isp == 2) {
          result.isp_mode = allowed_isp;
        } else {
          result.isp_mode = 1 + cabac.decodeBin(ctxs.isp_mode[1]);
        }
      }
    }
  }

  if (result.multi_ref_idx != 0) {
    result.mpm_flag = true;
  } else {
    result.mpm_flag = cabac.decodeBin(ctxs.ipred_mode[0]) != 0;
  }

  if (result.mpm_flag) {
    uint32_t ipred_idx = 0;
    if (result.multi_ref_idx == 0) {
      const int planar_ctx = result.isp_mode == 0 ? 1 : 0;
      ipred_idx = static_cast<uint32_t>(
          cabac.decodeBin(ctxs.intra_luma_planar_flag[planar_ctx]));
    } else {
      ipred_idx = 1;
    }

    while (ipred_idx < static_cast<uint32_t>(kNumMostProbableModes - 1) &&
           cabac.decodeBypass() != 0) {
      ++ipred_idx;
    }

    result.mpm_idx = static_cast<int>(ipred_idx);
    result.intra_luma_mode = result.mpm_candidates[result.mpm_idx];
    {
      std::ostringstream oss;
      oss << "mpm[" << result.mpm_idx << "]";
      result.intra_luma_source = oss.str();
    }
  } else {
    result.rem_intra_luma_pred_mode = static_cast<int>(
        decodeTruncBinCode(cabac, kNumLumaMode - kNumMostProbableModes));
    std::array<int, 6> sorted_mpm = result.mpm_candidates;
    std::sort(sorted_mpm.begin(), sorted_mpm.end());

    int intra_luma_mode = result.rem_intra_luma_pred_mode;
    for (int candidate : sorted_mpm) {
      if (intra_luma_mode >= candidate) ++intra_luma_mode;
    }

    result.intra_luma_mode = intra_luma_mode;
    {
      std::ostringstream oss;
      oss << "rem[" << result.rem_intra_luma_pred_mode << "]";
      result.intra_luma_source = oss.str();
    }
  }

  result.valid = true;
  return 0;
}
