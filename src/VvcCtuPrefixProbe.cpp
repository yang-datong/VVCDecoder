#include "VvcCtuPrefixProbe.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr int kMaxSaoTruncatedBitDepth = 10;
constexpr int kNumSaoBoClassesLog2 = 5;
constexpr int kNumSaoEoTypesLog2 = 2;
constexpr int kNumFixedFilterSets = 16;

template <size_t N>
void initContextArray(
    std::array<VvcCabacContextModel, N> &ctxs, int qp, int slice_type,
    const std::array<std::array<uint8_t, N>, 4> &init_table) {
  const int st = slice_type < 0 ? 0 : (slice_type > 2 ? 2 : slice_type);
  for (size_t i = 0; i < N; ++i) {
    ctxs[i].init(qp, init_table[st][i]);
    ctxs[i].setLog2WindowSize(init_table[3][i]);
  }
}

struct SaoContexts {
  std::array<VvcCabacContextModel, 1> sao_merge_flag;
  std::array<VvcCabacContextModel, 1> sao_type_idx;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 1>, 4> kSaoMergeInit = {{
        {{2}},
        {{60}},
        {{60}},
        {{0}},
    }};
    static const std::array<std::array<uint8_t, 1>, 4> kSaoTypeInit = {{
        {{2}},
        {{5}},
        {{13}},
        {{4}},
    }};
    initContextArray(sao_merge_flag, qp, slice_type, kSaoMergeInit);
    initContextArray(sao_type_idx, qp, slice_type, kSaoTypeInit);
  }
};

struct AlfContexts {
  std::array<VvcCabacContextModel, 9> ctb_alf_flag;
  std::array<VvcCabacContextModel, 1> alf_use_temporal_filt;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 9>, 4> kCtbAlfFlagInit = {{
        {{33, 52, 46, 25, 61, 54, 25, 61, 54}},
        {{13, 23, 46, 4, 61, 54, 19, 46, 54}},
        {{62, 39, 39, 54, 39, 39, 31, 39, 39}},
        {{0, 0, 0, 4, 0, 0, 1, 0, 0}},
    }};
    static const std::array<std::array<uint8_t, 1>, 4> kAlfUseTemporalInit = {{
        {{46}},
        {{46}},
        {{46}},
        {{0}},
    }};
    initContextArray(ctb_alf_flag, qp, slice_type, kCtbAlfFlagInit);
    initContextArray(alf_use_temporal_filt, qp, slice_type,
                     kAlfUseTemporalInit);
  }
};

int decodeBin(VvcCabacReader &cabac, VvcCabacContextModel &ctx, int &bins) {
  bins++;
  return cabac.decodeBin(ctx);
}

int decodeBypass(VvcCabacReader &cabac, int &bins) {
  bins++;
  return cabac.decodeBypass();
}

uint32_t decodeBypassBins(VvcCabacReader &cabac, int num_bins, int &bins) {
  uint32_t value = 0;
  for (int i = 0; i < num_bins; ++i) {
    value = (value << 1) | static_cast<uint32_t>(decodeBypass(cabac, bins) & 1);
  }
  return value;
}

uint32_t decodeTruncBinCode(VvcCabacReader &cabac, uint32_t max_symbol,
                            int &bins) {
  if (max_symbol <= 1) return 0;

  int thresh = 0;
  while ((1u << (thresh + 1)) <= max_symbol) {
    thresh++;
  }
  const uint32_t val = 1u << thresh;
  const uint32_t b = max_symbol - val;

  uint32_t symbol = decodeBypassBins(cabac, thresh, bins);
  if (symbol >= val - b) {
    symbol = (symbol << 1) | static_cast<uint32_t>(decodeBypass(cabac, bins) & 1);
    symbol -= (val - b);
  }
  return symbol;
}

uint32_t decodeUnaryMaxEqprob(VvcCabacReader &cabac, uint32_t max_symbol,
                              int &bins) {
  for (uint32_t k = 0; k < max_symbol; ++k) {
    if (decodeBypass(cabac, bins) == 0) return k;
  }
  return max_symbol;
}

int deriveMaxSaoOffsetQVal(int bit_depth) {
  const int truncated = std::min(bit_depth, kMaxSaoTruncatedBitDepth);
  return (1 << (truncated - 5)) - 1;
}

int consumeFirstCtuSao(VvcCabacReader &cabac, const VvcSpsState &sps,
                       int slice_type, int slice_qp_y,
                       const VvcFrameHeaderSummary &summary,
                       VvcCtuPrefixProbeResult &result, int &bins_consumed) {
  const bool sao_luma = summary.sao_luma_used_flag;
  const bool sao_chroma =
      summary.sao_chroma_used_flag && sps.chroma_format_idc != 0;
  if (!sao_luma && !sao_chroma) return 0;

  SaoContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  enum SaoMode { kOff, kBand, kEdge };
  SaoMode cb_mode = kOff;

  const int first_comp = sao_luma ? 0 : 1;
  const int last_comp = sao_chroma ? 2 : 0;
  for (int comp = first_comp; comp <= last_comp; ++comp) {
    SaoMode mode = kOff;
    if (comp != 2) {
      if (decodeBin(cabac, ctxs.sao_type_idx[0], bins_consumed) != 0) {
        mode = decodeBypass(cabac, bins_consumed) != 0 ? kEdge : kBand;
      }
      if (comp == 1) cb_mode = mode;
    } else {
      mode = cb_mode;
    }

    const char *mode_name =
        mode == kBand ? "band" : (mode == kEdge ? "edge" : "off");
    if (comp == 0) result.sao_luma_mode = mode_name;
    if (comp == 1) result.sao_chroma_mode = mode_name;

    if (mode == kOff) continue;

    const int max_offset_q_val = deriveMaxSaoOffsetQVal(sps.bit_depth);
    std::array<uint32_t, 4> offsets = {};
    for (int k = 0; k < 4; ++k) {
      offsets[k] =
          decodeUnaryMaxEqprob(cabac, static_cast<uint32_t>(max_offset_q_val),
                               bins_consumed);
    }
    if (comp == 0) {
      for (int k = 0; k < 4; ++k) {
        result.sao_luma_offsets[k] = static_cast<int>(offsets[k]);
      }
    }

    if (mode == kBand) {
      for (int k = 0; k < 4; ++k) {
        if (offsets[k] > 0) {
          (void)decodeBypass(cabac, bins_consumed);
        }
      }
      const uint32_t band_position =
          decodeBypassBins(cabac, kNumSaoBoClassesLog2, bins_consumed);
      if (comp == 0) {
        result.sao_luma_band_position = static_cast<int>(band_position);
      }
      continue;
    }

    if (comp != 2) {
      (void)decodeBypassBins(cabac, kNumSaoEoTypesLog2, bins_consumed);
    }
  }

  return 0;
}

int consumeFirstCtuAlf(VvcCabacReader &cabac, const VvcSpsState &sps,
                       int slice_type, int slice_qp_y,
                       const VvcFrameHeaderSummary &summary,
                       VvcCtuPrefixProbeResult &result, int &bins_consumed,
                       std::string &error) {
  if (!summary.alf_enabled_flag) return 0;

  if (summary.alf_cb_enabled_flag || summary.alf_cr_enabled_flag) {
    error = "First-CTU ALF prefix probe does not yet handle chroma ALF";
    return -1;
  }
  if (summary.alf_cc_cb_enabled_flag || summary.alf_cc_cr_enabled_flag) {
    error = "First-CTU ALF prefix probe does not yet handle CC-ALF";
    return -1;
  }
  if (!sps.use_alf) return 0;

  AlfContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  result.alf_luma_enabled =
      decodeBin(cabac, ctxs.ctb_alf_flag[0], bins_consumed) != 0;
  if (!result.alf_luma_enabled) return 0;

  const int num_aps = summary.num_alf_aps_ids_luma;
  result.alf_use_temporal =
      num_aps > 0 &&
      decodeBin(cabac, ctxs.alf_use_temporal_filt[0], bins_consumed) != 0;
  if (result.alf_use_temporal) {
    if (num_aps > 1) {
      (void)decodeTruncBinCode(cabac, static_cast<uint32_t>(num_aps),
                               bins_consumed);
    }
  } else {
    (void)decodeTruncBinCode(cabac, kNumFixedFilterSets, bins_consumed);
  }

  return 0;
}

} // namespace

int VvcCtuPrefixProbe::consumeFirstCtuPrefix(
    VvcCabacReader &cabac, const VvcSpsState &sps, int slice_type,
    int slice_qp_y, const VvcFrameHeaderSummary &summary,
    VvcCtuPrefixProbeResult &result) {
  m_last_error.clear();
  result = VvcCtuPrefixProbeResult{};

  if (!sps.valid) {
    setError("VVC CTU prefix probe requires valid SPS state");
    return -1;
  }

  int bins_consumed = 0;
  const int sao_bins_start = bins_consumed;
  if (consumeFirstCtuSao(cabac, sps, slice_type, slice_qp_y, summary, result,
                         bins_consumed) != 0) {
    setError("Failed to consume first-CTU SAO prefix");
    return -1;
  }
  result.sao_consumed = summary.sao_luma_used_flag || summary.sao_chroma_used_flag;
  result.sao_bins = bins_consumed - sao_bins_start;

  const int alf_bins_start = bins_consumed;
  if (consumeFirstCtuAlf(cabac, sps, slice_type, slice_qp_y, summary, result,
                         bins_consumed, m_last_error) != 0) {
    return -1;
  }
  result.alf_consumed = summary.alf_enabled_flag;
  result.alf_bins = bins_consumed - alf_bins_start;

  result.valid = true;
  result.bins_consumed = bins_consumed;
  return 0;
}
