#include "VvcResidualProbe.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {

constexpr int kJvetZeroOutTh = 32;
constexpr int kMaxLog2TuSizePlusOne = 7;
constexpr int kMlsGrpNum = 1024;
constexpr int kSbhThreshold = 4;
constexpr int kCoefRemainBinReduction = 5;
constexpr int kMaxTuLevelCtxCodedBinConstraintLuma = 28;
constexpr int kMaxLog2TrDynamicRange = 15;
constexpr int kDepQuantStateTransTable = 32040;

constexpr std::array<uint32_t, 32> kGoRiceParsCoeff = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
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

template <size_t Rows, size_t Cols>
void initContextMatrix(
    std::array<std::array<VvcCabacContextModel, Cols>, Rows> &ctxs, int qp,
    int slice_type,
    const std::array<std::array<std::array<uint8_t, Cols>, Rows>, 3>
        &init_table) {
  const int st = slice_type < 0 ? 0 : (slice_type > 2 ? 2 : slice_type);
  for (size_t row = 0; row < Rows; ++row) {
    for (size_t col = 0; col < Cols; ++col) {
      ctxs[row][col].init(qp, init_table[st][row][col]);
    }
  }
}

struct ResidualProbeContexts {
  std::array<VvcCabacContextModel, 2> sig_coeff_group;
  std::array<std::array<VvcCabacContextModel, 12>, 3> sig_flag;
  std::array<VvcCabacContextModel, 21> par_flag;
  std::array<VvcCabacContextModel, 21> greater2_flag;
  std::array<VvcCabacContextModel, 21> greater1_flag;

  void init(int qp, int slice_type) {
    static const std::array<std::array<uint8_t, 2>, 3> kSigCoeffGroupInit = {{
        {{25, 45}},
        {{25, 30}},
        {{18, 31}},
    }};
    static const std::array<std::array<std::array<uint8_t, 12>, 3>, 3>
        kSigFlagInit = {{
            {{
                {{17, 41, 49, 36, 1, 49, 50, 37, 48, 51, 58, 45}},
                {{26, 45, 53, 46, 49, 54, 61, 39, 35, 39, 39, 39}},
                {{19, 54, 39, 39, 50, 39, 39, 39, 0, 39, 39, 39}},
            }},
            {{
                {{17, 41, 42, 29, 25, 49, 43, 37, 33, 58, 51, 30}},
                {{19, 38, 38, 46, 34, 54, 54, 39, 6, 39, 39, 39}},
                {{19, 39, 54, 39, 19, 39, 39, 39, 56, 39, 39, 39}},
            }},
            {{
                {{25, 19, 28, 14, 25, 20, 29, 30, 19, 37, 30, 38}},
                {{11, 38, 46, 54, 27, 39, 39, 39, 44, 39, 39, 39}},
                {{18, 39, 39, 39, 27, 39, 39, 39, 0, 39, 39, 39}},
            }},
        }};
    static const std::array<std::array<uint8_t, 21>, 3> kParFlagInit = {{
        {{33, 40, 25, 41, 26, 42, 25, 33, 26, 34, 27,
          25, 41, 42, 42, 35, 33, 27, 35, 42, 43}},
        {{18, 17, 33, 18, 26, 42, 25, 33, 26, 42, 27,
          25, 34, 42, 42, 35, 26, 27, 42, 20, 20}},
        {{33, 25, 18, 26, 34, 27, 25, 26, 19, 42, 35,
          33, 19, 27, 35, 35, 34, 42, 20, 43, 20}},
    }};
    static const std::array<std::array<uint8_t, 21>, 3> kGreater2Init = {{
        {{25, 0, 0, 17, 25, 26, 0, 9, 25, 33, 19,
          0, 25, 33, 26, 20, 25, 33, 27, 35, 22}},
        {{17, 0, 1, 17, 25, 18, 0, 9, 25, 33, 34,
          9, 25, 18, 26, 20, 25, 18, 19, 27, 29}},
        {{25, 1, 40, 25, 33, 11, 17, 25, 25, 18, 4,
          17, 33, 26, 19, 13, 33, 19, 20, 28, 22}},
    }};
    static const std::array<std::array<uint8_t, 21>, 3> kGreater1Init = {{
        {{0, 0, 33, 34, 35, 21, 25, 34, 35, 28, 29,
          40, 42, 43, 29, 30, 49, 36, 37, 45, 38}},
        {{0, 17, 26, 19, 35, 21, 25, 34, 20, 28, 29,
          33, 27, 28, 29, 22, 34, 28, 44, 37, 38}},
        {{25, 25, 11, 27, 20, 21, 33, 12, 28, 21, 22,
          34, 28, 29, 29, 30, 36, 29, 45, 30, 23}},
    }};

    initContextArray(sig_coeff_group, qp, slice_type, kSigCoeffGroupInit);
    initContextMatrix(sig_flag, qp, slice_type, kSigFlagInit);
    initContextArray(par_flag, qp, slice_type, kParFlagInit);
    initContextArray(greater2_flag, qp, slice_type, kGreater2Init);
    initContextArray(greater1_flag, qp, slice_type, kGreater1Init);
  }
};

class ScanGenerator {
 public:
  ScanGenerator(uint32_t block_width, uint32_t block_height, uint32_t stride)
      : m_line(0), m_column(0), m_block_width(block_width),
        m_block_height(block_height), m_stride(stride) {}

  uint32_t currentX() const { return m_column; }
  uint32_t currentY() const { return m_line; }

  uint32_t nextIndex(uint32_t block_offset_x, uint32_t block_offset_y) {
    const uint32_t index =
        ((m_line + block_offset_y) * m_stride) + m_column + block_offset_x;

    if ((m_column == m_block_width - 1) || (m_line == 0)) {
      m_line += m_column + 1;
      m_column = 0;
      if (m_line >= m_block_height) {
        m_column += m_line - (m_block_height - 1);
        m_line = m_block_height - 1;
      }
    } else {
      ++m_column;
      --m_line;
    }

    return index;
  }

 private:
  uint32_t m_line;
  uint32_t m_column;
  const uint32_t m_block_width;
  const uint32_t m_block_height;
  const uint32_t m_stride;
};

int floorLog2(int value) {
  int log2 = -1;
  while (value > 0) {
    value >>= 1;
    ++log2;
  }
  return log2;
}

uint32_t goRicePosCoeff0(int state, uint32_t rice_par) {
  return static_cast<uint32_t>((state < 2 ? 1 : 2) << rice_par);
}

const uint16_t kLog2SbbSize[kMaxLog2TuSizePlusOne][kMaxLog2TuSizePlusOne][2] = {
    {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 4}, {0, 4}},
    {{1, 0}, {1, 1}, {1, 1}, {1, 3}, {1, 3}, {1, 3}, {1, 3}},
    {{2, 0}, {1, 1}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}},
    {{3, 0}, {3, 1}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}},
    {{4, 0}, {3, 1}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}},
    {{4, 0}, {3, 1}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}},
    {{4, 0}, {3, 1}, {2, 2}, {2, 2}, {2, 2}, {2, 2}, {2, 2}},
};

std::vector<uint16_t> buildUngroupedScan(uint32_t block_width,
                                         uint32_t block_height) {
  std::vector<uint16_t> scan;
  scan.reserve(block_width * block_height);
  ScanGenerator full_block_scan(block_width, block_height, block_width);
  for (uint32_t pos = 0; pos < block_width * block_height; ++pos) {
    scan.push_back(static_cast<uint16_t>(full_block_scan.nextIndex(0, 0)));
  }
  return scan;
}

std::vector<uint16_t> buildGroupedScan(uint32_t block_width,
                                       uint32_t block_height) {
  const uint32_t log2_cg_width =
      kLog2SbbSize[floorLog2(static_cast<int>(block_width))]
                  [floorLog2(static_cast<int>(block_height))][0];
  const uint32_t log2_cg_height =
      kLog2SbbSize[floorLog2(static_cast<int>(block_width))]
                  [floorLog2(static_cast<int>(block_height))][1];
  const uint32_t group_width = 1u << log2_cg_width;
  const uint32_t group_height = 1u << log2_cg_height;
  const uint32_t width_in_groups =
      std::min<uint32_t>(kJvetZeroOutTh, block_width) >> log2_cg_width;
  const uint32_t height_in_groups =
      std::min<uint32_t>(kJvetZeroOutTh, block_height) >> log2_cg_height;
  const uint32_t group_size = group_width * group_height;
  const uint32_t total_groups = width_in_groups * height_in_groups;

  std::vector<uint16_t> scan(total_groups * group_size, 0);
  ScanGenerator full_block_scan(width_in_groups, height_in_groups, group_width);
  for (uint32_t group_index = 0; group_index < total_groups; ++group_index) {
    const uint32_t group_position_y = full_block_scan.currentY();
    const uint32_t group_position_x = full_block_scan.currentX();
    const uint32_t group_offset_x = group_position_x * group_width;
    const uint32_t group_offset_y = group_position_y * group_height;
    const uint32_t group_offset_scan = group_index * group_size;

    ScanGenerator group_scan(group_width, group_height, block_width);
    for (uint32_t scan_position = 0; scan_position < group_size;
         ++scan_position) {
      scan[group_offset_scan + scan_position] = static_cast<uint16_t>(
          group_scan.nextIndex(group_offset_x, group_offset_y));
    }
    (void)full_block_scan.nextIndex(0, 0);
  }

  return scan;
}

struct CtxTpl {
  uint8_t ctx_tpl = 0;
};

struct ResidualCodingProbeContext {
  int width = 0;
  int height = 0;
  int width_in_groups = 0;
  int height_in_groups = 0;
  int log2_cg_width = 0;
  int log2_cg_height = 0;
  int log2_cg_size = 0;
  int log2_block_width = 0;
  int scan_pos_last = -1;
  int subset_id = -1;
  int subset_pos = -1;
  int subset_pos_x = -1;
  int subset_pos_y = -1;
  int min_sub_pos = -1;
  int max_sub_pos = -1;
  int sig_group_ctx_id = 0;
  int tmpl_cp_sum1 = -1;
  int tmpl_cp_diag = -1;
  int reg_bin_limit = 0;
  bool sign_hiding = false;
  std::vector<uint16_t> scan_cg;
  std::vector<uint16_t> scan;
  std::vector<CtxTpl> tpl;
  std::bitset<kMlsGrpNum> sig_group_flags;

  void init(int block_width, int block_height, bool use_sign_hiding) {
    width = block_width;
    height = block_height;
    log2_block_width = floorLog2(width);
    log2_cg_width = kLog2SbbSize[floorLog2(width)][floorLog2(height)][0];
    log2_cg_height = kLog2SbbSize[floorLog2(width)][floorLog2(height)][1];
    log2_cg_size = log2_cg_width + log2_cg_height;
    width_in_groups = std::min(kJvetZeroOutTh, width) >> log2_cg_width;
    height_in_groups = std::min(kJvetZeroOutTh, height) >> log2_cg_height;
    scan_cg = buildUngroupedScan(width_in_groups, height_in_groups);
    scan = buildGroupedScan(width, height);
    tpl.assign(static_cast<size_t>(width * height), CtxTpl{});
    sig_group_flags.reset();
    sign_hiding = use_sign_hiding;
    reg_bin_limit = (std::min(kJvetZeroOutTh, width) *
                     std::min(kJvetZeroOutTh, height) *
                     kMaxTuLevelCtxCodedBinConstraintLuma) >>
                    4;
  }

  void setScanPosLast(int pos) { scan_pos_last = pos; }

  int lastSubset() const { return scan_pos_last >> log2_cg_size; }

  void initSubblock(int sub_set_id) {
    subset_id = sub_set_id;
    subset_pos = scan_cg[static_cast<size_t>(subset_id)];
    subset_pos_y = subset_pos >> floorLog2(width_in_groups);
    subset_pos_x = subset_pos - (subset_pos_y * width_in_groups);
    min_sub_pos = subset_id << log2_cg_size;
    max_sub_pos = min_sub_pos + (1 << log2_cg_size) - 1;
    const bool last_hor_grp = subset_pos_x == width_in_groups - 1;
    const bool last_ver_grp = subset_pos_y == height_in_groups - 1;
    const unsigned sig_right =
        !last_hor_grp ? static_cast<unsigned>(sig_group_flags[subset_pos + 1])
                      : 0u;
    const unsigned sig_lower =
        !last_ver_grp
            ? static_cast<unsigned>(sig_group_flags[subset_pos + width_in_groups])
            : 0u;
    sig_group_ctx_id = static_cast<int>(sig_right | sig_lower);
    tmpl_cp_sum1 = -1;
    tmpl_cp_diag = -1;
  }

  bool isLast() const { return lastSubset() == subset_id; }
  bool isNotFirst() const { return subset_id != 0; }
  bool isSigGroup() const { return sig_group_flags[subset_pos]; }
  void setSigGroup() { sig_group_flags.set(subset_pos); }

  unsigned blockPos(int scan_pos) const {
    return scan[static_cast<size_t>(scan_pos)];
  }

  unsigned posX(int blk_pos) const {
    return static_cast<unsigned>(blk_pos & ((1 << log2_block_width) - 1));
  }

  unsigned posY(int blk_pos) const {
    return static_cast<unsigned>(blk_pos >> log2_block_width);
  }

  int scanPosOf(int blk_pos) const {
    for (size_t i = 0; i < scan.size(); ++i) {
      if (scan[i] == blk_pos) return static_cast<int>(i);
    }
    return -1;
  }

  unsigned sigCtxIdAbs(int blk_pos, int state) {
    const uint32_t pos_y = static_cast<uint32_t>(blk_pos >> log2_block_width);
    const uint32_t pos_x =
        static_cast<uint32_t>(blk_pos & ((1 << log2_block_width) - 1));
    const int diag = static_cast<int>(pos_x + pos_y);
    const int tpl_val = tpl[static_cast<size_t>(blk_pos)].ctx_tpl;
    const int num_pos = tpl_val >> 5;
    const int sum_abs = tpl_val & 31;

    int ctx_ofs = std::min((sum_abs + 1) >> 1, 3) + (diag < 2 ? 4 : 0);
    ctx_ofs += diag < 5 ? 4 : 0;

    tmpl_cp_diag = diag;
    tmpl_cp_sum1 = sum_abs - num_pos;
    const int ctx_set = std::max(0, std::min(2, state - 1));
    (void)ctx_set;
    return static_cast<unsigned>(ctx_ofs);
  }

  uint8_t ctxOffsetAbs() const {
    if (tmpl_cp_diag < 0) return 0;
    int offset = std::min(tmpl_cp_sum1, 4) + 1;
    offset += !tmpl_cp_diag ? 15
                            : (tmpl_cp_diag < 3 ? 10
                                                : (tmpl_cp_diag < 10 ? 5 : 0));
    return static_cast<uint8_t>(std::max(0, std::min(20, offset)));
  }

  void absVal1stPass(int blk_pos, std::vector<int> &coeff, int abs_level1) {
    coeff[static_cast<size_t>(blk_pos)] = abs_level1;

    const uint32_t pos_y = static_cast<uint32_t>(blk_pos >> log2_block_width);
    const uint32_t pos_x =
        static_cast<uint32_t>(blk_pos & ((1 << log2_block_width) - 1));

    auto update_deps = [&](int offset) {
      auto &ctx = tpl[static_cast<size_t>(blk_pos - offset)];
      ctx.ctx_tpl = static_cast<uint8_t>(ctx.ctx_tpl + 32 + abs_level1);
    };

    if (pos_y > 1) update_deps(2 * width);
    if (pos_y > 0 && pos_x > 0) update_deps(width + 1);
    if (pos_y > 0) update_deps(width);
    if (pos_x > 1) update_deps(2);
    if (pos_x > 0) update_deps(1);
  }

  unsigned templateAbsSum(int blk_pos, const std::vector<int> &coeff,
                          int base_level) const {
    const uint32_t pos_y = static_cast<uint32_t>(blk_pos >> log2_block_width);
    const uint32_t pos_x =
        static_cast<uint32_t>(blk_pos & ((1 << log2_block_width) - 1));
    const int *data = coeff.data() + pos_x + (pos_y << log2_block_width);
    int sum = 0;

    if (pos_x + 2 < static_cast<uint32_t>(width)) {
      sum += data[1];
      sum += data[2];
      if (pos_y + 1 < static_cast<uint32_t>(height)) {
        sum += data[width + 1];
      }
    } else if (pos_x + 1 < static_cast<uint32_t>(width)) {
      sum += data[1];
      if (pos_y + 1 < static_cast<uint32_t>(height)) {
        sum += data[width + 1];
      }
    }
    if (pos_y + 2 < static_cast<uint32_t>(height)) {
      sum += data[width];
      sum += data[width << 1];
    } else if (pos_y + 1 < static_cast<uint32_t>(height)) {
      sum += data[width];
    }

    return static_cast<unsigned>(std::max(std::min(sum - 5 * base_level, 31), 0));
  }

  bool hideSign(int pos_first, int pos_last) const {
    return sign_hiding && (pos_last - pos_first >= kSbhThreshold);
  }
};

struct ResidualSubblockDecodeResult {
  bool sig_group = false;
  int num_non_zero = 0;
  unsigned sign_pattern = 0;
  unsigned sub1_pattern = 0;
  std::vector<int> sig_positions;
};

int decodeResidualSubblock(
    VvcCabacReader &cabac, ResidualProbeContexts &ctxs,
    ResidualCodingProbeContext &cctx, std::vector<int> &coeff, bool dep_quant,
    int &state, ResidualSubblockDecodeResult &result) {
  result = ResidualSubblockDecodeResult{};

  const int min_sub_pos = cctx.min_sub_pos;
  const bool is_last = cctx.isLast();
  const int first_sig_pos = is_last ? cctx.scan_pos_last : cctx.max_sub_pos;
  int next_sig_pos = first_sig_pos;

  bool sig_group = is_last || min_sub_pos == 0;
  if (!sig_group) {
    sig_group =
        cabac.decodeBin(ctxs.sig_coeff_group[cctx.sig_group_ctx_id]) != 0;
  }
  result.sig_group = sig_group;
  if (!sig_group) {
    return 0;
  }
  cctx.setSigGroup();

  std::vector<int> gt1_positions;
  gt1_positions.reserve(16);

  const int infer_sig_pos =
      next_sig_pos != cctx.scan_pos_last ? (cctx.isNotFirst() ? min_sub_pos : -1)
                                         : next_sig_pos;
  int first_nz_pos = next_sig_pos;
  int last_nz_pos = -1;
  int num_non_zero = 0;
  int num_gt1 = 0;
  int rem_reg_bins = cctx.reg_bin_limit;
  unsigned gt2_mask = 0;
  unsigned state_val = 0;

  for (; next_sig_pos >= min_sub_pos && rem_reg_bins >= 4; --next_sig_pos) {
    const int blk_pos = static_cast<int>(cctx.blockPos(next_sig_pos));
    bool sig_flag = !num_non_zero && next_sig_pos == infer_sig_pos;
    (void)cctx.sigCtxIdAbs(blk_pos, state);

    unsigned abs_val = 0;
    if (!sig_flag) {
      const int ctx_set = std::max(0, std::min(2, state - 1));
      const unsigned sig_ctx_id = cctx.sigCtxIdAbs(blk_pos, state);
      sig_flag = cabac.decodeBin(ctxs.sig_flag[ctx_set][sig_ctx_id]) != 0;
      rem_reg_bins--;
    }

    if (sig_flag) {
      const uint8_t ctx_off = cctx.ctxOffsetAbs();
      state_val = ((state >> 1) & 1) | (state_val << 1);
      result.sig_positions.push_back(blk_pos);
      num_non_zero++;
      first_nz_pos = next_sig_pos;
      last_nz_pos = std::max(last_nz_pos, next_sig_pos);

      const unsigned gt1_flag = cabac.decodeBin(ctxs.greater1_flag[ctx_off]);
      rem_reg_bins--;

      if (gt1_flag != 0) {
        const unsigned par_flag = cabac.decodeBin(ctxs.par_flag[ctx_off]);
        rem_reg_bins--;
        const unsigned gt2_flag = cabac.decodeBin(ctxs.greater2_flag[ctx_off]);
        gt2_mask |= gt2_flag << num_gt1;
        rem_reg_bins--;
        gt1_positions.push_back(blk_pos);
        num_gt1++;
        abs_val = 2 + par_flag + (gt2_flag << 1);
        state = dep_quant
                    ? ((kDepQuantStateTransTable >>
                        ((state << 2) + (static_cast<int>(par_flag) << 1))) &
                       3)
                    : 0;
      } else {
        abs_val = 1;
        state = dep_quant ? ((kDepQuantStateTransTable >> ((state << 2) + 2)) & 3)
                          : 0;
      }

      cctx.absVal1stPass(blk_pos, coeff, static_cast<int>(abs_val));
    } else {
      state = dep_quant ? ((kDepQuantStateTransTable >> (state << 2)) & 3) : 0;
    }
  }

  cctx.reg_bin_limit = rem_reg_bins;

  for (int k = 0; k < num_gt1; ++k, gt2_mask >>= 1) {
    if ((gt2_mask & 1u) == 0) continue;
    const int blk_pos = gt1_positions[static_cast<size_t>(k)];
    const unsigned sum_all = cctx.templateAbsSum(blk_pos, coeff, 4);
    const unsigned rice_par = kGoRiceParsCoeff[sum_all];
    const int rem = static_cast<int>(cabac.decodeRemAbsEP(
        rice_par, kCoefRemainBinReduction, kMaxLog2TrDynamicRange));
    coeff[static_cast<size_t>(blk_pos)] += rem << 1;
  }

  for (; next_sig_pos >= min_sub_pos; --next_sig_pos) {
    const int sub1 = (state >> 1) & 1;
    const int blk_pos = static_cast<int>(cctx.blockPos(next_sig_pos));
    const unsigned sum_all = cctx.templateAbsSum(blk_pos, coeff, 0);
    const unsigned rice = kGoRiceParsCoeff[sum_all];
    const int pos0 = static_cast<int>(goRicePosCoeff0(state, rice));
    const int rem = static_cast<int>(cabac.decodeRemAbsEP(
        rice, kCoefRemainBinReduction, kMaxLog2TrDynamicRange));
    const int tcoeff = rem == pos0 ? 0 : (rem < pos0 ? rem + 1 : rem);

    state = dep_quant
                ? ((kDepQuantStateTransTable >>
                    ((state << 2) + ((tcoeff & 1) << 1))) &
                   3)
                : 0;
    if (tcoeff == 0) continue;

    coeff[static_cast<size_t>(blk_pos)] = tcoeff;
    state_val = static_cast<unsigned>(sub1) | (state_val << 1);
    result.sig_positions.push_back(blk_pos);
    num_non_zero++;
    first_nz_pos = next_sig_pos;
    last_nz_pos = std::max(last_nz_pos, next_sig_pos);
  }

  const unsigned num_signs =
      cctx.hideSign(first_nz_pos, last_nz_pos)
          ? static_cast<unsigned>(num_non_zero - 1)
          : static_cast<unsigned>(num_non_zero);
  unsigned sign_pattern =
      cabac.decodeBinsEP(static_cast<int>(num_signs));
  if (num_non_zero > static_cast<int>(num_signs)) {
    int sum_abs = 0;
    for (int blk_pos : result.sig_positions) {
      sum_abs += coeff[static_cast<size_t>(blk_pos)];
    }
    sign_pattern = (sign_pattern << 1) | static_cast<unsigned>(sum_abs & 1);
  }

  result.num_non_zero = num_non_zero;
  result.sign_pattern = sign_pattern;
  result.sub1_pattern = state_val;
  return 0;
}

} // namespace

int VvcResidualProbe::probeFirstTuResidualSyntax(
    VvcCabacReader &cabac, const VvcSpsState &sps, int slice_type,
    int slice_qp_y, bool dep_quant_used, bool sign_data_hiding_used,
    const VvcTransformTreeProbeResult &tu_result,
    VvcResidualProbeResult &result) {
  m_last_error.clear();
  result = VvcResidualProbeResult{};

  if (!sps.valid) {
    setError("VVC residual probe requires valid SPS state");
    return -1;
  }
  if (!tu_result.valid || tu_result.first_tu_cbf_y == 0 ||
      tu_result.first_tu_last_sig_x < 0 || tu_result.first_tu_last_sig_y < 0) {
    setError("VVC residual probe requires valid first TU residual state");
    return -1;
  }
  if (tu_result.first_tu_ts_flag != 0) {
    setError("Transform-skip residual syntax is not yet handled by residual probe");
    return -1;
  }

  ResidualProbeContexts ctxs;
  ctxs.init(slice_qp_y, slice_type);

  ResidualCodingProbeContext cctx;
  cctx.init(tu_result.first_tu_width, tu_result.first_tu_height,
            sign_data_hiding_used);

  result.coeffs.assign(
      static_cast<size_t>(tu_result.first_tu_width * tu_result.first_tu_height),
      0);
  std::vector<int> coeff_magnitudes(result.coeffs.size(), 0);

  const int last_blk_pos =
      tu_result.first_tu_last_sig_y * tu_result.first_tu_width +
      tu_result.first_tu_last_sig_x;
  const int scan_pos_last = cctx.scanPosOf(last_blk_pos);
  if (scan_pos_last < 0) {
    setError("Failed to map first TU last significant coefficient into scan order");
    return -1;
  }

  cctx.setScanPosLast(scan_pos_last);
  result.scan_pos_last = scan_pos_last;
  result.last_subset_id = cctx.lastSubset();

  int state = 0;
  for (int subset_id = result.last_subset_id; subset_id >= 0; --subset_id) {
    cctx.initSubblock(subset_id);

    ResidualSubblockDecodeResult subblock;
    if (decodeResidualSubblock(cabac, ctxs, cctx, coeff_magnitudes,
                               dep_quant_used, state, subblock) != 0) {
      setError("Failed to decode first TU residual subblock");
      return -1;
    }

    if (!subblock.sig_group) continue;

    ++result.sig_group_count;
    result.first_sig_subset_id = subset_id;

    unsigned sign_pattern = subblock.sign_pattern;
    unsigned sub1_pattern = subblock.sub1_pattern;
    for (int idx = subblock.num_non_zero - 1; idx >= 0;
         --idx, sign_pattern >>= 1, sub1_pattern >>= 1) {
      const int blk_pos = subblock.sig_positions[static_cast<size_t>(idx)];
      const int magnitude = dep_quant_used
                                ? (coeff_magnitudes[static_cast<size_t>(blk_pos)]
                                   << 1) -
                                      static_cast<int>(sub1_pattern & 1u)
                                : coeff_magnitudes[static_cast<size_t>(blk_pos)];
      result.coeffs[static_cast<size_t>(blk_pos)] =
          (sign_pattern & 1u) != 0 ? -magnitude : magnitude;
    }
  }

  for (int coeff : result.coeffs) {
    if (coeff == 0) continue;
    result.non_zero_coeffs++;
    result.abs_sum += std::abs(coeff);
    result.max_abs_level = std::max(result.max_abs_level, std::abs(coeff));
  }
  result.last_coeff_value = result.coeffs[static_cast<size_t>(last_blk_pos)];
  result.valid = true;
  return 0;
}
