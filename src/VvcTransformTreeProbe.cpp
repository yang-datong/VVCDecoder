#include "VvcTransformTreeProbe.hpp"
#include <array>
#include <cstdint>

namespace {

constexpr int kNotIntraSubpartitions = 0;
constexpr std::array<int, 8> kPrefixCtx = {{0, 0, 0, 3, 6, 10, 15, 21}};
constexpr std::array<uint32_t, 14> kMinInGroup = {
    {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96}};
constexpr std::array<uint32_t, 64> kGroupIdx = {{
    0,  1,  2,  3,  4,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
    8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
}};

template <size_t N>
void initContextArray(
    std::array<VvcCabacContextModel, N> &ctxs, int qp, int slice_type,
    const std::array<std::array<uint8_t, N>, 3> &init_table) {
  const int st = slice_type < 0 ? 0 : (slice_type > 2 ? 2 : slice_type);
  for (size_t i = 0; i < N; ++i) {
    ctxs[i].init(qp, init_table[st][i]);
  }
}

struct TransformProbeContexts {
  std::array<VvcCabacContextModel, 1> cclm_mode_flag;
  std::array<VvcCabacContextModel, 1> cclm_mode_idx;
  std::array<VvcCabacContextModel, 1> chroma_ipred_mode;
  std::array<VvcCabacContextModel, 4> qt_cbf_luma;
  std::array<VvcCabacContextModel, 2> qt_cbf_cb;
  std::array<VvcCabacContextModel, 3> qt_cbf_cr;
  std::array<VvcCabacContextModel, 6> mts_index;
  std::array<VvcCabacContextModel, 20> last_x_luma;
  std::array<VvcCabacContextModel, 20> last_y_luma;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 1>, 3> kCclmModeFlagInit = {{
        {{26}},
        {{34}},
        {{59}},
    }};
    static const std::array<std::array<uint8_t, 1>, 3> kCclmModeIdxInit = {{
        {{27}},
        {{27}},
        {{27}},
    }};
    static const std::array<std::array<uint8_t, 1>, 3> kChromaIPredModeInit = {{
        {{25}},
        {{25}},
        {{34}},
    }};
    static const std::array<std::array<uint8_t, 4>, 3> kQtCbfLumaInit = {{
        {{15, 6, 5, 14}},
        {{23, 5, 20, 7}},
        {{15, 12, 5, 7}},
    }};
    static const std::array<std::array<uint8_t, 2>, 3> kQtCbfCbInit = {{
        {{25, 37}},
        {{25, 28}},
        {{12, 21}},
    }};
    static const std::array<std::array<uint8_t, 3>, 3> kQtCbfCrInit = {{
        {{9, 36, 45}},
        {{25, 29, 45}},
        {{33, 28, 36}},
    }};
    static const std::array<std::array<uint8_t, 6>, 3> kMtsIndexInit = {{
        {{45, 25, 27, 0, 25, 17}},
        {{45, 40, 27, 0, 25, 9}},
        {{29, 0, 28, 0, 25, 9}},
    }};
    static const std::array<std::array<uint8_t, 20>, 3> kLastXLumaInit = {{
        {{6, 6, 12, 14, 6, 4, 14, 7, 6, 4, 29, 7, 6, 6, 12, 28, 7, 13, 13, 35}},
        {{6, 13, 12, 6, 6, 12, 14, 14, 13, 12, 29, 7, 6, 13, 36, 28, 14, 13, 5, 26}},
        {{13, 5, 4, 21, 14, 4, 6, 14, 21, 11, 14, 7, 14, 5, 11, 21, 30, 22, 13, 42}},
    }};
    static const std::array<std::array<uint8_t, 20>, 3> kLastYLumaInit = {{
        {{5, 5, 20, 13, 13, 19, 21, 6, 12, 12, 14, 14, 5, 4, 12, 13, 7, 13, 12, 41}},
        {{5, 5, 12, 6, 6, 4, 6, 14, 5, 12, 14, 7, 13, 5, 13, 21, 14, 20, 12, 34}},
        {{13, 5, 4, 6, 13, 11, 14, 6, 5, 3, 14, 22, 6, 4, 3, 6, 22, 29, 20, 34}},
    }};

    initContextArray(cclm_mode_flag, qp, slice_type, kCclmModeFlagInit);
    initContextArray(cclm_mode_idx, qp, slice_type, kCclmModeIdxInit);
    initContextArray(chroma_ipred_mode, qp, slice_type, kChromaIPredModeInit);
    initContextArray(qt_cbf_luma, qp, slice_type, kQtCbfLumaInit);
    initContextArray(qt_cbf_cb, qp, slice_type, kQtCbfCbInit);
    initContextArray(qt_cbf_cr, qp, slice_type, kQtCbfCrInit);
    initContextArray(mts_index, qp, slice_type, kMtsIndexInit);
    initContextArray(last_x_luma, qp, slice_type, kLastXLumaInit);
    initContextArray(last_y_luma, qp, slice_type, kLastYLumaInit);
  }
};

int chromaScaleX(int chroma_format_idc) {
  if (chroma_format_idc == 1 || chroma_format_idc == 2) return 1;
  return 0;
}

int chromaScaleY(int chroma_format_idc) {
  if (chroma_format_idc == 1) return 1;
  return 0;
}

bool allowCclmForProbe(const VvcSpsState &sps) {
  return !sps.dual_tree_intra_flag || sps.ctu_size <= 32;
}

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

int decodeLastSigPosition(VvcCabacReader &cabac,
                          std::array<VvcCabacContextModel, 20> &ctxs,
                          int block_size) {
  const int log2_block = floorLog2(block_size);
  const unsigned max_last_pos =
      kGroupIdx[static_cast<size_t>(std::min(32, block_size) - 1)];
  const unsigned ctx_offset =
      kPrefixCtx[static_cast<size_t>(std::max(0, log2_block))];
  const unsigned last_shift = static_cast<unsigned>((log2_block + 1) >> 2);

  unsigned pos = 0;
  for (; pos < max_last_pos; ++pos) {
    const unsigned ctx_idx = ctx_offset + (pos >> last_shift);
    if (ctx_idx >= ctxs.size()) break;
    if (cabac.decodeBin(ctxs[ctx_idx]) == 0) {
      break;
    }
  }

  if (pos > 3) {
    const unsigned count = (pos - 2) >> 1;
    pos = kMinInGroup[pos] + decodeBypassBins(cabac, static_cast<int>(count));
  }

  return static_cast<int>(pos);
}

} // namespace

int VvcTransformTreeProbe::probeFirstTransformUnit(
    VvcCabacReader &cabac, const VvcSpsState &sps, int slice_type,
    int slice_qp_y, const VvcSplitProbeResult &split_result,
    const VvcIntraCuProbeResult &intra_result,
    VvcTransformTreeProbeResult &result) {
  m_last_error.clear();
  result = VvcTransformTreeProbeResult{};

  if (!sps.valid) {
    setError("VVC transform tree probe requires valid SPS state");
    return -1;
  }
  if (!split_result.valid || !intra_result.valid) {
    setError("VVC transform tree probe requires valid split and intra probe state");
    return -1;
  }
  if (slice_type != I_SLICE) {
    setError("VVC transform tree probe currently only supports I slices");
    return -1;
  }

  TransformProbeContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  if (sps.chroma_format_idc != 0) {
    const int cu_chroma_width =
        split_result.leaf_width >> chromaScaleX(sps.chroma_format_idc);
    const int cu_chroma_height =
        split_result.leaf_height >> chromaScaleY(sps.chroma_format_idc);
    const int transform_skip_max_size =
        1 << sps.log2_max_transform_skip_block_size;
    const bool chroma_bdpcm_allowed =
        sps.bdpcm_enabled_flag && cu_chroma_width <= transform_skip_max_size &&
        cu_chroma_height <= transform_skip_max_size;
    if (chroma_bdpcm_allowed) {
      setError("Chroma BDPCM syntax is not yet handled by transform tree probe");
      return -1;
    }

    if (sps.cclm_enabled_flag && allowCclmForProbe(sps)) {
      result.chroma_cclm_flag =
          cabac.decodeBin(ctxs.cclm_mode_flag[0]) != 0;
      if (result.chroma_cclm_flag) {
        const int lm_symbol = cabac.decodeBin(ctxs.cclm_mode_idx[0]);
        if (lm_symbol != 0) {
          (void)cabac.decodeBypass();
        }
      } else {
        const int chroma_dm =
            cabac.decodeBin(ctxs.chroma_ipred_mode[0]) == 0 ? 1 : 0;
        result.chroma_dm_mode = chroma_dm != 0;
        if (!result.chroma_dm_mode) {
          result.chroma_cand_idx =
              (cabac.decodeBypass() << 1) | cabac.decodeBypass();
        }
      }
    } else {
      const int chroma_dm =
          cabac.decodeBin(ctxs.chroma_ipred_mode[0]) == 0 ? 1 : 0;
      result.chroma_dm_mode = chroma_dm != 0;
      if (!result.chroma_dm_mode) {
        result.chroma_cand_idx =
            (cabac.decodeBypass() << 1) | cabac.decodeBypass();
      }
    }
  }

  int tu_width = split_result.leaf_width;
  int tu_height = split_result.leaf_height;
  int tu_depth = 0;
  while (tu_width > sps.max_tb_size || tu_height > sps.max_tb_size) {
    tu_width >>= 1;
    tu_height >>= 1;
    ++tu_depth;
  }

  result.first_tu_width = tu_width;
  result.first_tu_height = tu_height;
  result.first_tu_depth = tu_depth;

  if (sps.chroma_format_idc != 0) {
    result.first_tu_cbf_cb = cabac.decodeBin(ctxs.qt_cbf_cb[0]);
    result.first_tu_cbf_cr =
        cabac.decodeBin(ctxs.qt_cbf_cr[result.first_tu_cbf_cb ? 1 : 0]);
  }

  result.first_tu_cbf_y =
      cabac.decodeBin(ctxs.qt_cbf_luma[kNotIntraSubpartitions]);

  const int transform_skip_max_size =
      1 << sps.log2_max_transform_skip_block_size;
  const bool ts_allowed =
      sps.transform_skip_enabled_flag && intra_result.isp_mode == 0 &&
      result.first_tu_width <= transform_skip_max_size &&
      result.first_tu_height <= transform_skip_max_size;
  if (result.first_tu_cbf_y != 0) {
    if (ts_allowed) {
      result.first_tu_ts_flag_coded = true;
      result.first_tu_ts_flag = cabac.decodeBin(ctxs.mts_index[4]);
    }
    if (result.first_tu_ts_flag == 0) {
      result.first_tu_last_sig_x =
          decodeLastSigPosition(cabac, ctxs.last_x_luma, result.first_tu_width);
      result.first_tu_last_sig_y =
          decodeLastSigPosition(cabac, ctxs.last_y_luma, result.first_tu_height);
    }
  }

  result.valid = true;
  return 0;
}
