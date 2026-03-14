#include "VvcSplitProbe.hpp"
#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class SplitKind {
  kCtuLevel,
  kNone,
  kQuad,
  kHoriz,
  kVert,
  kTriHoriz,
  kTriVert,
};

struct SplitNodeState {
  int width = 0;
  int height = 0;
  int qt_depth = 0;
  int mt_depth = 0;
  int implicit_bt_depth = 0;
  SplitKind last_split = SplitKind::kCtuLevel;
};

struct SplitConstraints {
  int min_qt_size = 0;
  int max_mtt_depth = 0;
  int max_bt_size = 0;
  int max_tt_size = 0;
  int min_bt_size = 0;
  int min_tt_size = 0;
};

template <size_t N>
void initContextArray(
    std::array<VvcCabacContextModel, N> &ctxs, int qp, int slice_type,
    const std::array<std::array<uint8_t, N>, 3> &init_table) {
  const int st = slice_type < 0 ? 0 : (slice_type > 2 ? 2 : slice_type);
  for (size_t i = 0; i < N; ++i) {
    ctxs[i].init(qp, init_table[st][i]);
  }
}

struct SplitContexts {
  std::array<VvcCabacContextModel, 9> split_flag;
  std::array<VvcCabacContextModel, 6> split_qt_flag;
  std::array<VvcCabacContextModel, 5> split_hv_flag;
  std::array<VvcCabacContextModel, 4> split_12_flag;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 9>, 3> kSplitFlagInit = {{
        {{18, 27, 15, 18, 28, 45, 26, 7, 23}},
        {{11, 35, 53, 12, 6, 30, 13, 15, 31}},
        {{19, 28, 38, 27, 29, 38, 20, 30, 31}},
    }};
    static const std::array<std::array<uint8_t, 6>, 3> kSplitQtInit = {{
        {{26, 36, 38, 18, 34, 21}},
        {{20, 14, 23, 18, 19, 6}},
        {{27, 6, 15, 25, 19, 37}},
    }};
    static const std::array<std::array<uint8_t, 5>, 3> kSplitHvInit = {{
        {{43, 42, 37, 42, 44}},
        {{43, 35, 37, 34, 52}},
        {{43, 42, 29, 27, 44}},
    }};
    static const std::array<std::array<uint8_t, 4>, 3> kSplit12Init = {{
        {{28, 29, 28, 29}},
        {{43, 37, 21, 22}},
        {{36, 45, 36, 45}},
    }};

    initContextArray(split_flag, qp, slice_type, kSplitFlagInit);
    initContextArray(split_qt_flag, qp, slice_type, kSplitQtInit);
    initContextArray(split_hv_flag, qp, slice_type, kSplitHvInit);
    initContextArray(split_12_flag, qp, slice_type, kSplit12Init);
  }
};

void deriveCanSplit(const SplitNodeState &node, const SplitConstraints &cons,
                    bool &can_no, bool &can_qt, bool &can_bh, bool &can_bv,
                    bool &can_th, bool &can_tv) {
  can_no = can_qt = can_bh = can_bv = can_th = can_tv = true;

  bool can_btt = node.mt_depth < (cons.max_mtt_depth + node.implicit_bt_depth);
  if (node.last_split != SplitKind::kCtuLevel &&
      node.last_split != SplitKind::kQuad) {
    can_qt = false;
  }
  if (node.width <= cons.min_qt_size) {
    can_qt = false;
  }

  can_btt &= node.width > cons.min_bt_size || node.height > cons.min_bt_size ||
             node.width > cons.min_tt_size || node.height > cons.min_tt_size;
  can_btt &= (node.width <= cons.max_bt_size && node.height <= cons.max_bt_size) ||
             (node.width <= cons.max_tt_size && node.height <= cons.max_tt_size);

  if (!can_btt) {
    can_bh = can_bv = can_th = can_tv = false;
    return;
  }

  if (node.width > cons.max_bt_size || node.height > cons.max_bt_size) {
    can_bh = false;
    can_bv = false;
  } else {
    can_bh &= node.height > cons.min_bt_size && node.height <= cons.max_bt_size;
    can_bv &= node.width > cons.min_bt_size && node.width <= cons.max_bt_size;
  }

  if (node.width > cons.max_tt_size || node.height > cons.max_tt_size) {
    can_th = false;
    can_tv = false;
    return;
  }

  can_th &= !(node.height <= 2 * cons.min_tt_size);
  can_tv &= !(node.width <= 2 * cons.min_tt_size);
}

const char *splitName(SplitKind split) {
  switch (split) {
  case SplitKind::kNone:
    return "N";
  case SplitKind::kQuad:
    return "Q";
  case SplitKind::kHoriz:
    return "H";
  case SplitKind::kVert:
    return "V";
  case SplitKind::kTriHoriz:
    return "TH";
  case SplitKind::kTriVert:
    return "TV";
  default:
    return "UNK";
  }
}

} // namespace

int VvcSplitProbe::probeFirstCuSplitPath(VvcCabacReader &cabac,
                                         const VvcSpsState &sps,
                                         const VvcPictureHeaderState &picture,
                                         int slice_type, int slice_qp_y,
                                         VvcSplitProbeResult &result) {
  m_last_error.clear();
  result = VvcSplitProbeResult{};

  if (!sps.valid || !picture.valid) {
    setError("VVC split probe requires valid SPS and picture header state");
    return -1;
  }
  if (sps.ctu_size <= 0 || sps.log2_min_cb_size <= 0) {
    setError("Invalid SPS partition constraints");
    return -1;
  }

  SplitConstraints cons;
  const int cfg_idx = (slice_type == I_SLICE) ? 0 : 1;
  cons.min_qt_size = picture.partition_constraints_override_flag
                         ? picture.min_qt_sizes[cfg_idx]
                         : sps.min_qt_sizes[cfg_idx];
  cons.max_mtt_depth = picture.partition_constraints_override_flag
                           ? picture.max_mtt_depths[cfg_idx]
                           : sps.max_mtt_depths[cfg_idx];
  cons.max_bt_size = picture.partition_constraints_override_flag
                         ? picture.max_bt_sizes[cfg_idx]
                         : sps.max_bt_sizes[cfg_idx];
  cons.max_tt_size = picture.partition_constraints_override_flag
                         ? picture.max_tt_sizes[cfg_idx]
                         : sps.max_tt_sizes[cfg_idx];
  cons.min_bt_size = 1 << sps.log2_min_cb_size;
  cons.min_tt_size = 1 << sps.log2_min_cb_size;

  if (cons.min_qt_size <= 0 || cons.max_bt_size <= 0 || cons.max_tt_size <= 0) {
    setError("SPS/PH split constraints are not usable for split probe");
    return -1;
  }

  SplitContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  SplitNodeState node;
  node.width = sps.ctu_size;
  node.height = sps.ctu_size;
  node.last_split = SplitKind::kCtuLevel;

  std::vector<std::string> path_nodes;
  int decision_count = 0;
  constexpr int kMaxProbeDepth = 64;

  for (int depth = 0; depth < kMaxProbeDepth; ++depth) {
    bool can_no = false;
    bool can_qt = false;
    bool can_bh = false;
    bool can_bv = false;
    bool can_th = false;
    bool can_tv = false;
    deriveCanSplit(node, cons, can_no, can_qt, can_bh, can_bv, can_th, can_tv);

    const int num_hor = static_cast<int>(can_bh) + static_cast<int>(can_th);
    const int num_ver = static_cast<int>(can_bv) + static_cast<int>(can_tv);
    const int num_split = (can_qt ? 2 : 0) + num_hor + num_ver;
    if (num_split <= 0) {
      path_nodes.push_back(splitName(SplitKind::kNone));
      break;
    }

    bool is_split = true;
    if (can_no) {
      static const int kCtxOffset[7] = {0, 0, 0, 3, 3, 6, 6};
      if (num_split < 0 || num_split > 6) {
        std::ostringstream oss;
        oss << "Unexpected split candidate count: " << num_split;
        setError(oss.str());
        return -1;
      }
      const int ctx_split = kCtxOffset[num_split];
      is_split = cabac.decodeBin(ctxs.split_flag[ctx_split]) != 0;
      decision_count++;
    }

    if (!is_split) {
      path_nodes.push_back(splitName(SplitKind::kNone));
      break;
    }

    const bool can_btt = (num_hor > 0) || (num_ver > 0);
    bool is_qt = can_qt;
    if (is_qt && can_btt) {
      const int ctx_qt_split = node.qt_depth < 2 ? 0 : 3;
      is_qt = cabac.decodeBin(ctxs.split_qt_flag[ctx_qt_split]) != 0;
      decision_count++;
    }

    SplitKind split = SplitKind::kQuad;
    if (!is_qt) {
      const bool can_hor = num_hor > 0;
      bool is_ver = num_ver > 0;
      if (is_ver && can_hor) {
        int ctx_btt_hv = 0;
        if (num_ver == num_hor) {
          ctx_btt_hv = 0;
        } else if (num_ver < num_hor) {
          ctx_btt_hv = 3;
        } else {
          ctx_btt_hv = 4;
        }
        is_ver = cabac.decodeBin(ctxs.split_hv_flag[ctx_btt_hv]) != 0;
        decision_count++;
      }

      const bool can_14 = is_ver ? can_tv : can_th;
      bool is_12 = is_ver ? can_bv : can_bh;
      if (is_12 && can_14) {
        const int ctx_btt_12 = static_cast<int>(node.mt_depth <= 1) + (is_ver ? 2 : 0);
        is_12 = cabac.decodeBin(ctxs.split_12_flag[ctx_btt_12]) != 0;
        decision_count++;
      }

      if (is_ver && is_12)
        split = SplitKind::kVert;
      else if (is_ver && !is_12)
        split = SplitKind::kTriVert;
      else if (!is_ver && is_12)
        split = SplitKind::kHoriz;
      else
        split = SplitKind::kTriHoriz;
    }

    path_nodes.push_back(splitName(split));
    node.last_split = split;
    switch (split) {
    case SplitKind::kQuad:
      node.width >>= 1;
      node.height >>= 1;
      node.qt_depth++;
      node.mt_depth = 0;
      break;
    case SplitKind::kHoriz:
      node.height >>= 1;
      node.mt_depth++;
      break;
    case SplitKind::kVert:
      node.width >>= 1;
      node.mt_depth++;
      break;
    case SplitKind::kTriHoriz:
      node.height >>= 2;
      node.mt_depth++;
      break;
    case SplitKind::kTriVert:
      node.width >>= 2;
      node.mt_depth++;
      break;
    default:
      break;
    }

    if (node.width <= 0 || node.height <= 0) {
      setError("Invalid split probe state: block size became non-positive");
      return -1;
    }
  }

  if (path_nodes.empty()) {
    setError("Split probe did not decode any syntax decision");
    return -1;
  }

  std::ostringstream oss;
  for (size_t i = 0; i < path_nodes.size(); ++i) {
    if (i > 0) oss << '>';
    oss << path_nodes[i];
  }

  result.valid = true;
  result.decisions = decision_count;
  result.leaf_width = node.width;
  result.leaf_height = node.height;
  result.path = oss.str();
  return 0;
}
