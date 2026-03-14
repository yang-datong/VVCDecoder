#include "VvcMiniParser.hpp"
#include <cmath>
#include <sstream>

namespace {

constexpr int kVvcNalHeaderBytes = 2;

struct VvcBitReader {
  explicit VvcBitReader(BitStream &stream) : bs(stream) {}

  BitStream &bs;

  uint32_t flag() { return bs.readU1(); }
  uint32_t code(int bits) { return bs.readUn(bits); }
  uint32_t uvlc() { return bs.readUE(); }
  int32_t svlc() { return static_cast<int32_t>(bs.readSE()); }

  bool moreRbspData() { return bs.more_rbsp_data(); }

  int bitsUntilByteAligned() const {
    const int bits_left = bs.getBitsLeft();
    return bits_left == 8 ? 0 : bits_left;
  }

  int bitsRead() const {
    return static_cast<int>((bs.getP() - bs.getBufStart()) * 8) +
           (8 - bs.getBitsLeft());
  }

  void byteAlign() {
    while (bitsUntilByteAligned() != 0) {
      flag();
    }
  }
};

const char *kUnsupportedPrefix = "Unsupported VVC syntax subset: ";

bool setUnsupported(std::string &error, const std::string &detail) {
  error = std::string(kUnsupportedPrefix) + detail;
  return false;
}

bool skipConstraintInfo(VvcBitReader &reader, std::string &error) {
  const bool gci_present_flag = reader.flag();
  if (!gci_present_flag) {
    reader.byteAlign();
    return true;
  }

  // The current incremental parser targets the demo/simple stream subset where
  // gci_present_flag is 0. Supporting the full constraint-info payload is not
  // necessary for this stage.
  return setUnsupported(error, "gci_present_flag=1");
}

bool skipProfileTierLevel(VvcBitReader &reader, bool profile_tier_present_flag,
                          int max_num_sub_layers_minus1,
                          std::string &error) {
  if (profile_tier_present_flag) {
    reader.code(7);
    reader.flag();
  }

  reader.code(8);
  reader.flag();
  reader.flag();

  if (profile_tier_present_flag &&
      !skipConstraintInfo(reader, error)) {
    return false;
  }

  std::array<bool, 7> sub_layer_level_present = {};
  for (int i = max_num_sub_layers_minus1 - 1; i >= 0; --i) {
    sub_layer_level_present[i] = reader.flag() != 0;
  }

  reader.byteAlign();

  for (int i = max_num_sub_layers_minus1 - 1; i >= 0; --i) {
    if (sub_layer_level_present[i]) reader.code(8);
  }

  if (profile_tier_present_flag) {
    const uint32_t ptl_num_sub_profiles = reader.code(8);
    for (uint32_t i = 0; i < ptl_num_sub_profiles; ++i) {
      reader.code(32);
    }
  }

  return true;
}

void skipDpbParameters(VvcBitReader &reader, int max_num_sub_layers_minus1,
                       bool sub_layer_info_flag) {
  for (int i = sub_layer_info_flag ? 0 : max_num_sub_layers_minus1;
       i <= max_num_sub_layers_minus1; ++i) {
    (void)i;
    reader.uvlc();
    reader.uvlc();
    reader.uvlc();
  }
}

void skipQpTables(VvcBitReader &reader, int num_qp_tables) {
  for (int i = 0; i < num_qp_tables; ++i) {
    reader.svlc();
    const uint32_t num_points_in_qp_table_minus1 = reader.uvlc();
    for (uint32_t j = 0; j <= num_points_in_qp_table_minus1; ++j) {
      reader.uvlc();
      reader.uvlc();
    }
  }
}

int skipRefPicList(VvcBitReader &reader, bool long_term_refs_present_flag,
                   bool weighted_pred_flag, bool weighted_bipred_flag,
                   int bits_for_poc) {
  const uint32_t num_ref_entries = reader.uvlc();
  bool ltrp_in_header_flag = long_term_refs_present_flag;
  if (long_term_refs_present_flag && num_ref_entries > 0) {
    ltrp_in_header_flag = reader.flag() != 0;
  }

  for (uint32_t ii = 0; ii < num_ref_entries; ++ii) {
    bool is_long_term = false;
    if (long_term_refs_present_flag) {
      const bool st_ref_pic_flag = reader.flag() != 0;
      is_long_term = !st_ref_pic_flag;
    }

    if (!is_long_term) {
      int delta_poc_st = static_cast<int>(reader.uvlc());
      if ((!weighted_pred_flag && !weighted_bipred_flag) || ii == 0) {
        delta_poc_st++;
      }
      if (delta_poc_st > 0) {
        reader.flag();
      }
    } else if (!ltrp_in_header_flag) {
      reader.code(bits_for_poc);
    }
  }
  return static_cast<int>(num_ref_entries);
}

bool consumeByteAlignment(VvcBitReader &reader, std::string &error) {
  if (reader.flag() == 0) {
    error = "Invalid VVC byte alignment: missing leading 1 bit";
    return false;
  }
  while (reader.bitsUntilByteAligned() != 0) {
    if (reader.flag() != 0) {
      error = "Invalid VVC byte alignment: expected zero padding bits";
      return false;
    }
  }
  return true;
}

bool isIdrLikeNal(uint8_t nal_unit_type) {
  return nal_unit_type == VVC_NAL_UNIT_CODED_SLICE_IDR_W_RADL ||
         nal_unit_type == VVC_NAL_UNIT_CODED_SLICE_IDR_N_LP;
}

const VvcSpsState *findSps(const std::array<VvcSpsState, 16> &spss,
                           int sps_id) {
  if (sps_id < 0 || sps_id >= static_cast<int>(spss.size())) return nullptr;
  return spss[sps_id].valid ? &spss[sps_id] : nullptr;
}

const VvcPpsState *findPps(const std::array<VvcPpsState, 64> &ppss,
                           int pps_id) {
  if (pps_id < 0 || pps_id >= static_cast<int>(ppss.size())) return nullptr;
  return ppss[pps_id].valid ? &ppss[pps_id] : nullptr;
}

int deriveNumRefIdxActive(int pps_default_active, int rpl_entries,
                          bool override_flag, int override_minus1) {
  if (rpl_entries <= 0) return 0;
  if (override_flag) return std::min(rpl_entries, override_minus1 + 1);
  return std::min(rpl_entries, pps_default_active);
}

bool parsePictureHeaderPayload(
    VvcBitReader &reader, const std::array<VvcSpsState, 16> &spss,
    const std::array<VvcPpsState, 64> &ppss, VvcPictureHeaderState &picture,
    std::string &error) {
  picture = VvcPictureHeaderState{};

  picture.gdr_or_irap_pic_flag = reader.flag() != 0;
  picture.non_ref_pic_flag = reader.flag() != 0;
  if (picture.gdr_or_irap_pic_flag) {
    picture.gdr_pic_flag = reader.flag() != 0;
  }

  picture.inter_slice_allowed_flag = reader.flag() != 0;
  picture.intra_slice_allowed_flag =
      picture.inter_slice_allowed_flag ? (reader.flag() != 0) : true;
  if (!picture.inter_slice_allowed_flag && !picture.intra_slice_allowed_flag) {
    error = "Invalid VVC picture header: both inter and intra slices are disallowed";
    return false;
  }

  picture.pps_id = static_cast<int>(reader.uvlc());
  const VvcPpsState *pps = findPps(ppss, picture.pps_id);
  if (pps == nullptr) {
    std::ostringstream oss;
    oss << "Missing VVC PPS id " << picture.pps_id;
    error = oss.str();
    return false;
  }

  picture.sps_id = pps->sps_id;
  const VvcSpsState *sps = findSps(spss, picture.sps_id);
  if (sps == nullptr) {
    std::ostringstream oss;
    oss << "Missing VVC SPS id " << picture.sps_id;
    error = oss.str();
    return false;
  }

  picture.min_qt_sizes = sps->min_qt_sizes;
  picture.max_mtt_depths = sps->max_mtt_depths;
  picture.max_bt_sizes = sps->max_bt_sizes;
  picture.max_tt_sizes = sps->max_tt_sizes;

  picture.poc_lsb = static_cast<int>(reader.code(sps->bits_for_poc));
  if (picture.gdr_pic_flag) {
    (void)reader.uvlc();
  }

  for (bool present : sps->extra_ph_bit_present_flags) {
    if (present) {
      reader.flag();
    }
  }

  if (sps->poc_msb_cycle_flag) {
    picture.poc_msb_present_flag = reader.flag() != 0;
    if (picture.poc_msb_present_flag) {
      picture.poc_msb_val =
          static_cast<int>(reader.code(sps->poc_msb_cycle_len_minus1 + 1));
    }
  }

  if (sps->use_alf && pps->alf_info_in_ph_flag) {
    return setUnsupported(error, "pps_alf_info_in_ph_flag=1");
  }

  if (sps->use_lmcs) {
    const bool ph_lmcs_enabled_flag = reader.flag() != 0;
    if (ph_lmcs_enabled_flag) {
      reader.code(2);
      if (sps->chroma_format_idc != 0) {
        reader.flag();
      }
    }
  }

  if (sps->scaling_list_enabled_flag) {
    const bool ph_explicit_scaling_list_enabled_flag = reader.flag() != 0;
    if (ph_explicit_scaling_list_enabled_flag) {
      reader.code(3);
    }
  }

  if (sps->virtual_boundaries_enabled_flag &&
      !sps->virtual_boundaries_present_flag) {
    const bool ph_virtual_boundaries_present_flag = reader.flag() != 0;
    if (ph_virtual_boundaries_present_flag) {
      const uint32_t num_ver_virtual_boundaries = reader.uvlc();
      for (uint32_t i = 0; i < num_ver_virtual_boundaries; ++i) {
        reader.uvlc();
      }

      const uint32_t num_hor_virtual_boundaries = reader.uvlc();
      for (uint32_t i = 0; i < num_hor_virtual_boundaries; ++i) {
        reader.uvlc();
      }
    }
  }

  if (pps->output_flag_present_flag && !picture.non_ref_pic_flag) {
    reader.flag();
  }

  if (pps->rpl_info_in_ph_flag) {
    return setUnsupported(error, "pps_rpl_info_in_ph_flag=1");
  }

  if (sps->split_cons_override_enabled_flag) {
    picture.partition_constraints_override_flag = reader.flag() != 0;
    if (picture.partition_constraints_override_flag) {
      return setUnsupported(error, "ph_partition_constraints_override_flag=1");
    }
  }

  if (picture.intra_slice_allowed_flag) {
    if (pps->cu_qp_delta_enabled_flag) {
      picture.cu_qp_delta_subdiv_intra_slice = static_cast<int>(reader.uvlc());
    }
    if (pps->cu_chroma_qp_offset_list_enabled_flag) {
      (void)reader.uvlc();
    }
  }

  if (picture.inter_slice_allowed_flag) {
    if (pps->cu_qp_delta_enabled_flag) {
      picture.cu_qp_delta_subdiv_inter_slice = static_cast<int>(reader.uvlc());
    }
    if (pps->cu_chroma_qp_offset_list_enabled_flag) {
      (void)reader.uvlc();
    }

    if (sps->temporal_mvp_enabled_flag) {
      picture.temporal_mvp_enabled_flag = reader.flag() != 0;
      if (picture.temporal_mvp_enabled_flag && pps->rpl_info_in_ph_flag) {
        return setUnsupported(error,
                              "ph_temporal_mvp_enabled_flag with RPL in PH");
      }
    }

    if (sps->mmvd_fullpel_only_enabled_flag) {
      picture.mmvd_fullpel_only_flag = reader.flag() != 0;
    }

    const bool presence_flag = !pps->rpl_info_in_ph_flag;
    if (presence_flag) {
      picture.mvd_l1_zero_flag = reader.flag() != 0;
      picture.bdof_disabled_flag =
          sps->bdof_control_present_in_ph_flag ? (reader.flag() != 0)
                                               : !sps->bdof_enabled_flag;
      picture.dmvr_disabled_flag =
          sps->dmvr_control_present_in_ph_flag ? (reader.flag() != 0)
                                               : !sps->dmvr_enabled_flag;
    }

    picture.prof_disabled_flag =
        sps->prof_control_present_in_ph_flag ? (reader.flag() != 0) : true;

    if ((pps->wp_info_in_ph_flag) &&
        (pps->cu_qp_delta_enabled_flag || pps->slice_chroma_qp_offsets_present_flag)) {
      return setUnsupported(error, "weighted prediction in PH");
    }
  }

  if (pps->qp_delta_info_in_ph_flag) {
    (void)reader.svlc();
  }

  if (sps->joint_cbcr_enabled_flag) {
    picture.joint_cbcr_sign_flag = reader.flag() != 0;
  }

  if (sps->use_sao && pps->sao_info_in_ph_flag) {
    reader.flag();
    if (sps->chroma_format_idc != 0) {
      reader.flag();
    }
  }

  if (pps->dbf_info_in_ph_flag) {
    const bool ph_deblocking_params_present_flag = reader.flag() != 0;
    bool deblocking_disabled = pps->pps_deblocking_filter_disabled_flag;
    if (ph_deblocking_params_present_flag) {
      if (!pps->pps_deblocking_filter_disabled_flag) {
        deblocking_disabled = reader.flag() != 0;
      }
      if (!deblocking_disabled) {
        (void)reader.svlc();
        (void)reader.svlc();
        if (pps->chroma_tool_offsets_present_flag) {
          (void)reader.svlc();
          (void)reader.svlc();
          (void)reader.svlc();
          (void)reader.svlc();
        }
      }
    }
  }

  if (pps->picture_header_extension_present_flag) {
    const uint32_t ph_extension_length = reader.uvlc();
    for (uint32_t i = 0; i < ph_extension_length; ++i) {
      reader.code(8);
    }
  }

  picture.valid = true;
  return true;
}

bool parseSliceHeaderTail(VvcBitReader &reader, const Nalu &nalu,
                          const VvcSpsState &sps, const VvcPpsState &pps,
                          const VvcPictureHeaderState &picture,
                          VvcFrameHeaderSummary &summary, std::string &error) {
  summary.slice_qp_y = 26 + pps.init_qp_minus26;

  if (sps.use_alf && !pps.alf_info_in_ph_flag) {
    summary.alf_enabled_flag = reader.flag() != 0;
    if (summary.alf_enabled_flag) {
      const int num_alf_aps_ids_luma = static_cast<int>(reader.code(3));
      for (int i = 0; i < num_alf_aps_ids_luma; ++i) {
        reader.code(3);
      }

      bool alf_cb_enabled_flag = false;
      bool alf_cr_enabled_flag = false;
      if (sps.chroma_format_idc != 0) {
        alf_cb_enabled_flag = reader.flag() != 0;
        alf_cr_enabled_flag = reader.flag() != 0;
      }
      if (alf_cb_enabled_flag || alf_cr_enabled_flag) {
        reader.code(3);
      }

      if (sps.use_ccalf) {
        const bool alf_cc_cb_enabled_flag = reader.flag() != 0;
        if (alf_cc_cb_enabled_flag) {
          reader.code(3);
        }
        const bool alf_cc_cr_enabled_flag = reader.flag() != 0;
        if (alf_cc_cr_enabled_flag) {
          reader.code(3);
        }
      }
    }
  }

  if (sps.use_lmcs && !summary.picture_header_in_slice_header_flag) {
    return setUnsupported(error, "sh_lmcs_used_flag");
  }
  if (sps.scaling_list_enabled_flag && !summary.picture_header_in_slice_header_flag) {
    return setUnsupported(error, "sh_explicit_scaling_list_used_flag");
  }

  int num_ref_entries[2] = {0, 0};
  if (summary.slice_type != I_SLICE) {
    if (!(isIdrLikeNal(nalu.nal_unit_type) && !sps.idr_rpl_present_flag)) {
      bool rpl_sps_flag[2] = {false, false};
      for (int list_idx = 0; list_idx < (summary.slice_type == B_SLICE ? 2 : 1);
           ++list_idx) {
        if (list_idx == 0 || pps.rpl1_idx_present_flag) {
          rpl_sps_flag[list_idx] = reader.flag() != 0;
        } else {
          rpl_sps_flag[list_idx] = rpl_sps_flag[0];
        }
        if (rpl_sps_flag[list_idx]) {
          return setUnsupported(error, "rpl_sps_flag=1");
        }
        num_ref_entries[list_idx] =
            skipRefPicList(reader, sps.long_term_ref_pics_flag,
                           sps.weighted_pred_flag, sps.weighted_bipred_flag,
                           sps.bits_for_poc);
      }
    }

    bool num_ref_idx_active_override_flag = false;
    int num_ref_idx_active_minus1[2] = {0, 0};
    if (num_ref_entries[0] > 1 ||
        (summary.slice_type == B_SLICE && num_ref_entries[1] > 1)) {
      num_ref_idx_active_override_flag = reader.flag() != 0;
      if (num_ref_idx_active_override_flag) {
        if (num_ref_entries[0] > 1) {
          num_ref_idx_active_minus1[0] = static_cast<int>(reader.uvlc());
        }
        if (summary.slice_type == B_SLICE && num_ref_entries[1] > 1) {
          num_ref_idx_active_minus1[1] = static_cast<int>(reader.uvlc());
        }
      }
    }

    const int num_ref_idx_l0_active = deriveNumRefIdxActive(
        pps.num_ref_idx_l0_default_active, num_ref_entries[0],
        num_ref_idx_active_override_flag, num_ref_idx_active_minus1[0]);
    const int num_ref_idx_l1_active = deriveNumRefIdxActive(
        pps.num_ref_idx_l1_default_active, num_ref_entries[1],
        num_ref_idx_active_override_flag, num_ref_idx_active_minus1[1]);

    if (pps.cabac_init_present_flag) {
      reader.flag();
    }

    if (picture.temporal_mvp_enabled_flag && !pps.rpl_info_in_ph_flag) {
      bool collocated_from_l0_flag = true;
      if (summary.slice_type == B_SLICE) {
        collocated_from_l0_flag = reader.flag() != 0;
      }
      if ((collocated_from_l0_flag && num_ref_idx_l0_active > 1) ||
          (!collocated_from_l0_flag && num_ref_idx_l1_active > 1)) {
        (void)reader.uvlc();
      }
    }

    if (!pps.wp_info_in_ph_flag &&
        ((pps.weighted_pred_flag && summary.slice_type == P_SLICE) ||
         (pps.weighted_bipred_flag && summary.slice_type == B_SLICE))) {
      return setUnsupported(error, "weighted prediction in slice header");
    }
  }

  if (!pps.qp_delta_info_in_ph_flag) {
    summary.slice_qp_delta = reader.svlc();
    summary.slice_qp_y = 26 + pps.init_qp_minus26 + summary.slice_qp_delta;
  }

  if (pps.slice_chroma_qp_offsets_present_flag) {
    (void)reader.svlc();
    (void)reader.svlc();
    if (sps.joint_cbcr_enabled_flag) {
      (void)reader.svlc();
    }
  }

  if (pps.cu_chroma_qp_offset_list_enabled_flag) {
    (void)reader.flag();
  }

  if (sps.use_sao && !pps.sao_info_in_ph_flag) {
    summary.sao_luma_used_flag = reader.flag() != 0;
    if (sps.chroma_format_idc != 0) {
      summary.sao_chroma_used_flag = reader.flag() != 0;
    }
  }

  if (pps.deblocking_filter_control_present_flag && !pps.dbf_info_in_ph_flag) {
    return setUnsupported(error, "slice deblocking filter override");
  }

  if (sps.dep_quant_enabled_flag) {
    summary.dep_quant_used_flag = reader.flag() != 0;
  }

  if (sps.sign_data_hiding_enabled_flag && !summary.dep_quant_used_flag) {
    summary.sign_data_hiding_used_flag = reader.flag() != 0;
  }

  if (sps.transform_skip_enabled_flag && !summary.dep_quant_used_flag &&
      !summary.sign_data_hiding_used_flag) {
    summary.ts_residual_coding_disabled_flag = reader.flag() != 0;
  }

  if (pps.slice_header_extension_present_flag) {
    return setUnsupported(error, "pps_slice_header_extension_present_flag=1");
  }

  if (!consumeByteAlignment(reader, error)) {
    return false;
  }

  summary.payload_rbsp_bit_offset = reader.bitsRead();
  summary.payload_rbsp_byte_offset = summary.payload_rbsp_bit_offset / 8;
  summary.payload_bit_offset =
      summary.payload_rbsp_bit_offset + (kVvcNalHeaderBytes * 8);
  summary.payload_byte_offset = summary.payload_bit_offset / 8;
  return true;
}

} // namespace

int VvcMiniParser::parseSps(BitStream &bs, VvcSpsState &sps) {
  m_last_error.clear();
  VvcBitReader reader(bs);
  sps = VvcSpsState{};

  sps.sps_id = static_cast<int>(reader.code(4));
  const int sps_video_parameter_set_id = static_cast<int>(reader.code(4));
  sps.max_sublayers_minus1 = static_cast<int>(reader.code(3));
  sps.chroma_format_idc = static_cast<int>(reader.code(2));
  const int sps_log2_ctu_size_minus5 = static_cast<int>(reader.code(2));
  sps.ctu_size = 1 << (sps_log2_ctu_size_minus5 + 5);

  const bool sps_ptl_dpb_hrd_params_present_flag = reader.flag() != 0;
  if (!skipProfileTierLevel(reader, sps_ptl_dpb_hrd_params_present_flag,
                            sps.max_sublayers_minus1, m_last_error)) {
    return -1;
  }

  (void)reader.flag(); // sps_gdr_enabled_flag
  const bool sps_ref_pic_resampling_enabled_flag = reader.flag() != 0;
  if (sps_ref_pic_resampling_enabled_flag) {
    (void)reader.flag();
  }

  sps.max_pic_width_in_luma_samples = static_cast<int>(reader.uvlc());
  sps.max_pic_height_in_luma_samples = static_cast<int>(reader.uvlc());

  const bool sps_conformance_window_flag = reader.flag() != 0;
  if (sps_conformance_window_flag) {
    (void)reader.uvlc();
    (void)reader.uvlc();
    (void)reader.uvlc();
    (void)reader.uvlc();
  }

  sps.subpic_info_present_flag = reader.flag() != 0;
  if (sps.subpic_info_present_flag) {
    setError(std::string(kUnsupportedPrefix) +
             "sps_subpic_info_present_flag=1");
    return -1;
  }

  const int sps_bitdepth_minus8 = static_cast<int>(reader.uvlc());
  sps.qp_bd_offset = 6 * sps_bitdepth_minus8;
  sps.entropy_coding_sync_enabled_flag = reader.flag() != 0;
  sps.entry_point_offsets_present_flag = reader.flag() != 0;

  sps.bits_for_poc = static_cast<int>(reader.code(4)) + 4;
  sps.poc_msb_cycle_flag = reader.flag() != 0;
  if (sps.poc_msb_cycle_flag) {
    sps.poc_msb_cycle_len_minus1 = static_cast<int>(reader.uvlc());
  }

  sps.num_extra_ph_bytes = static_cast<int>(reader.code(2));
  sps.extra_ph_bit_present_flags.assign(8 * sps.num_extra_ph_bytes, false);
  for (int i = 0; i < 8 * sps.num_extra_ph_bytes; ++i) {
    sps.extra_ph_bit_present_flags[i] = reader.flag() != 0;
  }

  sps.num_extra_sh_bytes = static_cast<int>(reader.code(2));
  sps.extra_sh_bit_present_flags.assign(8 * sps.num_extra_sh_bytes, false);
  for (int i = 0; i < 8 * sps.num_extra_sh_bytes; ++i) {
    sps.extra_sh_bit_present_flags[i] = reader.flag() != 0;
  }

  if (sps_ptl_dpb_hrd_params_present_flag) {
    bool sps_sublayer_dpb_params_flag = false;
    if (sps.max_sublayers_minus1 > 0) {
      sps_sublayer_dpb_params_flag = reader.flag() != 0;
    }
    skipDpbParameters(reader, sps.max_sublayers_minus1,
                      sps_sublayer_dpb_params_flag);
  }

  const int min_cb_log2_size = static_cast<int>(reader.uvlc()) + 2;
  sps.log2_min_cb_size = min_cb_log2_size;
  sps.split_cons_override_enabled_flag = reader.flag() != 0;

  const int min_qt_log2_intra_luma =
      static_cast<int>(reader.uvlc()) + min_cb_log2_size;
  sps.min_qt_sizes[0] = 1 << min_qt_log2_intra_luma;
  const uint32_t sps_max_mtt_hierarchy_depth_intra_slice_luma = reader.uvlc();
  sps.max_mtt_depths[0] =
      static_cast<int>(sps_max_mtt_hierarchy_depth_intra_slice_luma);
  sps.max_bt_sizes[0] = sps.min_qt_sizes[0];
  sps.max_tt_sizes[0] = sps.min_qt_sizes[0];
  if (sps_max_mtt_hierarchy_depth_intra_slice_luma != 0) {
    sps.max_bt_sizes[0] <<= static_cast<int>(reader.uvlc());
    sps.max_tt_sizes[0] <<= static_cast<int>(reader.uvlc());
  }

  bool sps_qtbtt_dual_tree_intra_flag = false;
  if (sps.chroma_format_idc != 0) {
    sps_qtbtt_dual_tree_intra_flag = reader.flag() != 0;
  }
  sps.dual_tree_intra_flag = sps_qtbtt_dual_tree_intra_flag;
  if (sps_qtbtt_dual_tree_intra_flag) {
    const int min_qt_log2_intra_chroma =
        static_cast<int>(reader.uvlc()) + min_cb_log2_size;
    sps.min_qt_sizes[2] = 1 << min_qt_log2_intra_chroma;
    const uint32_t sps_max_mtt_hierarchy_depth_intra_slice_chroma = reader.uvlc();
    sps.max_mtt_depths[2] =
        static_cast<int>(sps_max_mtt_hierarchy_depth_intra_slice_chroma);
    sps.max_bt_sizes[2] = sps.min_qt_sizes[2];
    sps.max_tt_sizes[2] = sps.min_qt_sizes[2];
    if (sps_max_mtt_hierarchy_depth_intra_slice_chroma != 0) {
      sps.max_bt_sizes[2] <<= static_cast<int>(reader.uvlc());
      sps.max_tt_sizes[2] <<= static_cast<int>(reader.uvlc());
    }
  }

  const int min_qt_log2_inter =
      static_cast<int>(reader.uvlc()) + min_cb_log2_size;
  sps.min_qt_sizes[1] = 1 << min_qt_log2_inter;
  const uint32_t sps_max_mtt_hierarchy_depth_inter_slice = reader.uvlc();
  sps.max_mtt_depths[1] =
      static_cast<int>(sps_max_mtt_hierarchy_depth_inter_slice);
  sps.max_bt_sizes[1] = sps.min_qt_sizes[1];
  sps.max_tt_sizes[1] = sps.min_qt_sizes[1];
  if (sps_max_mtt_hierarchy_depth_inter_slice != 0) {
    sps.max_bt_sizes[1] <<= static_cast<int>(reader.uvlc());
    sps.max_tt_sizes[1] <<= static_cast<int>(reader.uvlc());
  }

  bool sps_max_luma_transform_size_64_flag = false;
  if (sps.ctu_size > 32) {
    sps_max_luma_transform_size_64_flag = reader.flag() != 0;
  }
  sps.max_tb_size =
      1 << (5 + (sps.ctu_size > 32 ? sps_max_luma_transform_size_64_flag : 0));
  sps.transform_skip_enabled_flag = reader.flag() != 0;
  if (sps.transform_skip_enabled_flag) {
    (void)reader.uvlc();
    sps.bdpcm_enabled_flag = reader.flag() != 0;
  }

  sps.mts_enabled_flag = reader.flag() != 0;
  if (sps.mts_enabled_flag) {
    (void)reader.flag();
    (void)reader.flag();
  }
  sps.lfnst_enabled_flag = reader.flag() != 0;

  if (sps.chroma_format_idc != 0) {
    sps.joint_cbcr_enabled_flag = reader.flag() != 0;
    const bool sps_same_qp_table_for_chroma_flag = reader.flag() != 0;
    const int num_qp_tables = sps_same_qp_table_for_chroma_flag
                                  ? 1
                                  : (sps.joint_cbcr_enabled_flag ? 3 : 2);
    skipQpTables(reader, num_qp_tables);
  }

  sps.use_sao = reader.flag() != 0;
  sps.use_alf = reader.flag() != 0;
  sps.use_ccalf = sps.use_alf && sps.chroma_format_idc != 0
                      ? (reader.flag() != 0)
                      : false;
  sps.use_lmcs = reader.flag() != 0;
  sps.weighted_pred_flag = reader.flag() != 0;
  sps.weighted_bipred_flag = reader.flag() != 0;
  sps.long_term_ref_pics_flag = reader.flag() != 0;

  if (sps_video_parameter_set_id > 0) {
    return setUnsupported(m_last_error, "sps_video_parameter_set_id>0") ? 0 : -1;
  }

  sps.idr_rpl_present_flag = reader.flag() != 0;
  const bool sps_rpl1_same_as_rpl0_flag = reader.flag() != 0;
  const int rpl_list_count = sps_rpl1_same_as_rpl0_flag ? 1 : 2;
  for (int i = 0; i < rpl_list_count; ++i) {
    const uint32_t sps_num_ref_pic_lists = reader.uvlc();
    for (uint32_t j = 0; j < sps_num_ref_pic_lists; ++j) {
      (void)skipRefPicList(reader, sps.long_term_ref_pics_flag,
                           sps.weighted_pred_flag, sps.weighted_bipred_flag,
                           sps.bits_for_poc);
    }
  }

  (void)reader.flag(); // sps_ref_wraparound_enabled_flag
  sps.temporal_mvp_enabled_flag = reader.flag() != 0;
  sps.sbtmvp_enabled_flag =
      sps.temporal_mvp_enabled_flag ? (reader.flag() != 0) : false;

  const bool sps_amvr_enabled_flag = reader.flag() != 0;
  sps.bdof_enabled_flag = reader.flag() != 0;
  sps.bdof_control_present_in_ph_flag =
      sps.bdof_enabled_flag ? (reader.flag() != 0) : false;

  (void)reader.flag(); // sps_smvd_enabled_flag
  sps.dmvr_enabled_flag = reader.flag() != 0;
  sps.dmvr_control_present_in_ph_flag =
      sps.dmvr_enabled_flag ? (reader.flag() != 0) : false;

  sps.mmvd_enabled_flag = reader.flag() != 0;
  sps.mmvd_fullpel_only_enabled_flag =
      sps.mmvd_enabled_flag ? (reader.flag() != 0) : false;

  const int max_num_merge_cand =
      6 - static_cast<int>(reader.uvlc());
  (void)reader.flag(); // sps_sbt_enabled_flag

  sps.affine_enabled_flag = reader.flag() != 0;
  if (sps.affine_enabled_flag) {
    (void)reader.uvlc();
    (void)reader.flag();
    if (sps_amvr_enabled_flag) {
      (void)reader.flag();
    }
    const bool sps_affine_prof_enabled_flag = reader.flag() != 0;
    sps.prof_control_present_in_ph_flag =
        sps_affine_prof_enabled_flag ? (reader.flag() != 0) : false;
  }

  (void)reader.flag(); // sps_bcw_enabled_flag
  (void)reader.flag(); // sps_ciip_enabled_flag
  if (max_num_merge_cand >= 2) {
    const bool sps_gpm_enabled_flag = reader.flag() != 0;
    if (sps_gpm_enabled_flag && max_num_merge_cand >= 3) {
      (void)reader.uvlc();
    }
  }

  (void)reader.uvlc(); // sps_log2_parallel_merge_level_minus2
  sps.isp_enabled_flag = reader.flag() != 0;
  sps.mrl_enabled_flag = reader.flag() != 0;
  sps.mip_enabled_flag = reader.flag() != 0;
  if (sps.chroma_format_idc != 0) {
    (void)reader.flag(); // sps_cclm_enabled_flag
  }
  if (sps.chroma_format_idc == 1) {
    (void)reader.flag();
    (void)reader.flag();
  }

  const bool sps_palette_enabled_flag = reader.flag() != 0;
  if (sps_palette_enabled_flag) {
    setError(std::string(kUnsupportedPrefix) + "sps_palette_enabled_flag=1");
    return -1;
  }

  const bool use_color_transform = false;
  if (sps.transform_skip_enabled_flag || sps_palette_enabled_flag) {
    (void)reader.uvlc();
  }

  const bool sps_ibc_enabled_flag = reader.flag() != 0;
  if (sps_ibc_enabled_flag) {
    (void)reader.uvlc();
  }

  const bool sps_ladf_enabled_flag = reader.flag() != 0;
  if (sps_ladf_enabled_flag) {
    const uint32_t sps_num_ladf_intervals_minus2 = reader.code(2);
    (void)reader.svlc();
    for (uint32_t i = 0; i < sps_num_ladf_intervals_minus2 + 1; ++i) {
      (void)reader.svlc();
      (void)reader.uvlc();
    }
  }

  sps.scaling_list_enabled_flag = reader.flag() != 0;
  if (sps.scaling_list_enabled_flag) {
    if (sps.lfnst_enabled_flag) {
      (void)reader.flag();
    }
    if (use_color_transform) {
      const bool disabled = reader.flag() != 0;
      if (disabled) {
        (void)reader.flag();
      }
    }
  }

  sps.dep_quant_enabled_flag = reader.flag() != 0;
  sps.sign_data_hiding_enabled_flag = reader.flag() != 0;
  sps.virtual_boundaries_enabled_flag = reader.flag() != 0;
  if (sps.virtual_boundaries_enabled_flag) {
    sps.virtual_boundaries_present_flag = reader.flag() != 0;
    if (sps.virtual_boundaries_present_flag) {
      const uint32_t num_ver_virtual_boundaries = reader.uvlc();
      for (uint32_t i = 0; i < num_ver_virtual_boundaries; ++i) {
        (void)reader.uvlc();
      }

      const uint32_t num_hor_virtual_boundaries = reader.uvlc();
      for (uint32_t i = 0; i < num_hor_virtual_boundaries; ++i) {
        (void)reader.uvlc();
      }
    }
  }

  sps.valid = true;
  return 0;
}

int VvcMiniParser::parsePps(BitStream &bs,
                            const std::array<VvcSpsState, 16> &spss,
                            VvcPpsState &pps) {
  m_last_error.clear();
  VvcBitReader reader(bs);
  pps = VvcPpsState{};

  pps.pps_id = static_cast<int>(reader.code(6));
  pps.sps_id = static_cast<int>(reader.code(4));

  const VvcSpsState *sps = findSps(spss, pps.sps_id);
  if (sps == nullptr) {
    std::ostringstream oss;
    oss << "Missing VVC SPS id " << pps.sps_id << " for PPS";
    setError(oss.str());
    return -1;
  }

  (void)reader.flag(); // pps_mixed_nalu_types_in_pic_flag
  pps.pic_width_in_luma_samples = static_cast<int>(reader.uvlc());
  pps.pic_height_in_luma_samples = static_cast<int>(reader.uvlc());

  const bool pps_conformance_window_flag = reader.flag() != 0;
  if (pps_conformance_window_flag) {
    (void)reader.uvlc();
    (void)reader.uvlc();
    (void)reader.uvlc();
    (void)reader.uvlc();
  }

  const bool pps_scaling_window_explicit_signalling_flag = reader.flag() != 0;
  if (pps_scaling_window_explicit_signalling_flag) {
    (void)reader.svlc();
    (void)reader.svlc();
    (void)reader.svlc();
    (void)reader.svlc();
  }

  pps.output_flag_present_flag = reader.flag() != 0;
  pps.no_pic_partition_flag = reader.flag() != 0;
  if (!pps.no_pic_partition_flag) {
    setError(std::string(kUnsupportedPrefix) + "pps_no_pic_partition_flag=0");
    return -1;
  }

  const bool pps_subpic_id_mapping_present_flag = reader.flag() != 0;
  if (pps_subpic_id_mapping_present_flag) {
    setError(std::string(kUnsupportedPrefix) +
             "pps_subpic_id_mapping_present_flag=1");
    return -1;
  }

  pps.cabac_init_present_flag = reader.flag() != 0;
  pps.num_ref_idx_l0_default_active = static_cast<int>(reader.uvlc()) + 1;
  pps.num_ref_idx_l1_default_active = static_cast<int>(reader.uvlc()) + 1;
  pps.rpl1_idx_present_flag = reader.flag() != 0;
  pps.weighted_pred_flag = reader.flag() != 0;
  pps.weighted_bipred_flag = reader.flag() != 0;
  (void)reader.flag(); // pps_ref_wraparound_enabled_flag
  pps.init_qp_minus26 = reader.svlc();
  pps.cu_qp_delta_enabled_flag = reader.flag() != 0;
  pps.chroma_tool_offsets_present_flag = reader.flag() != 0;

  if (pps.chroma_tool_offsets_present_flag) {
    (void)reader.svlc();
    (void)reader.svlc();

    pps.joint_cbcr_qp_offset_present_flag = reader.flag() != 0;
    if (pps.joint_cbcr_qp_offset_present_flag) {
      (void)reader.svlc();
    }

    pps.slice_chroma_qp_offsets_present_flag = reader.flag() != 0;
    pps.cu_chroma_qp_offset_list_enabled_flag = reader.flag() != 0;
    if (pps.cu_chroma_qp_offset_list_enabled_flag) {
      const uint32_t pps_chroma_qp_offset_list_len_minus1 = reader.uvlc();
      for (uint32_t i = 0; i <= pps_chroma_qp_offset_list_len_minus1; ++i) {
        (void)reader.svlc();
        (void)reader.svlc();
        if (pps.joint_cbcr_qp_offset_present_flag) {
          (void)reader.svlc();
        }
      }
    }
  }

  pps.deblocking_filter_control_present_flag = reader.flag() != 0;
  if (pps.deblocking_filter_control_present_flag) {
    pps.deblocking_filter_override_enabled_flag = reader.flag() != 0;
    pps.pps_deblocking_filter_disabled_flag = reader.flag() != 0;
    if (!pps.pps_deblocking_filter_disabled_flag) {
      (void)reader.svlc();
      (void)reader.svlc();
      if (pps.chroma_tool_offsets_present_flag) {
        (void)reader.svlc();
        (void)reader.svlc();
        (void)reader.svlc();
        (void)reader.svlc();
      }
    }
  }

  // With pps_no_pic_partition_flag equal to 1, the *_in_ph flags are inferred
  // to be 0 and are not present.
  pps.rpl_info_in_ph_flag = false;
  pps.sao_info_in_ph_flag = false;
  pps.alf_info_in_ph_flag = false;
  pps.wp_info_in_ph_flag = false;
  pps.qp_delta_info_in_ph_flag = false;
  pps.dbf_info_in_ph_flag = false;

  pps.picture_header_extension_present_flag = reader.flag() != 0;
  pps.slice_header_extension_present_flag = reader.flag() != 0;

  const bool pps_extension_flag = reader.flag() != 0;
  if (pps_extension_flag) {
    while (reader.moreRbspData()) {
      (void)reader.flag();
    }
  }

  pps.valid = true;
  return 0;
}

int VvcMiniParser::parsePictureHeader(
    BitStream &bs, const std::array<VvcSpsState, 16> &spss,
    const std::array<VvcPpsState, 64> &ppss,
    VvcPictureHeaderState &picture_header) {
  m_last_error.clear();
  VvcBitReader reader(bs);
  if (!parsePictureHeaderPayload(reader, spss, ppss, picture_header,
                                 m_last_error)) {
    return -1;
  }
  picture_header.valid = true;
  return 0;
}

bool VvcMiniParser::isIrayOrGdrNal(uint8_t nal_unit_type) {
  switch (nal_unit_type) {
  case VVC_NAL_UNIT_CODED_SLICE_IDR_W_RADL:
  case VVC_NAL_UNIT_CODED_SLICE_IDR_N_LP:
  case VVC_NAL_UNIT_CODED_SLICE_CRA:
  case VVC_NAL_UNIT_CODED_SLICE_GDR:
    return true;
  default:
    return false;
  }
}

bool VvcMiniParser::isIdrNal(uint8_t nal_unit_type) {
  return nal_unit_type == VVC_NAL_UNIT_CODED_SLICE_IDR_W_RADL ||
         nal_unit_type == VVC_NAL_UNIT_CODED_SLICE_IDR_N_LP;
}

int VvcMiniParser::parseSliceHeader(
    const Nalu &nalu, BitStream &bs, const std::array<VvcSpsState, 16> &spss,
    const std::array<VvcPpsState, 64> &ppss, int prev_tid0_poc,
    const VvcPictureHeaderState *active_picture_header,
    VvcPictureHeaderState &parsed_picture_header,
    VvcFrameHeaderSummary &summary) {
  m_last_error.clear();
  VvcBitReader reader(bs);
  parsed_picture_header = VvcPictureHeaderState{};
  summary = VvcFrameHeaderSummary{};

  summary.picture_header_in_slice_header_flag = reader.flag() != 0;
  if (summary.picture_header_in_slice_header_flag) {
    if (!parsePictureHeaderPayload(reader, spss, ppss, parsed_picture_header,
                                   m_last_error)) {
      return -1;
    }
    parsed_picture_header.in_slice_header = true;
  } else {
    if (active_picture_header == nullptr || !active_picture_header->valid) {
      setError("Missing active VVC picture header for slice");
      return -1;
    }
    parsed_picture_header = *active_picture_header;
  }

  const VvcPpsState *pps = findPps(ppss, parsed_picture_header.pps_id);
  if (pps == nullptr) {
    std::ostringstream oss;
    oss << "Missing VVC PPS id " << parsed_picture_header.pps_id
        << " for slice header";
    setError(oss.str());
    return -1;
  }

  const VvcSpsState *sps = findSps(spss, pps->sps_id);
  if (sps == nullptr) {
    std::ostringstream oss;
    oss << "Missing VVC SPS id " << pps->sps_id << " for slice header";
    setError(oss.str());
    return -1;
  }

  if (sps->subpic_info_present_flag) {
    setError(std::string(kUnsupportedPrefix) +
             "slice header with subpicture info");
    return -1;
  }

  if (!pps->no_pic_partition_flag) {
    setError(std::string(kUnsupportedPrefix) +
             "slice header with picture partitioning");
    return -1;
  }

  for (bool present : sps->extra_sh_bit_present_flags) {
    if (present) {
      reader.flag();
    }
  }

  summary.slice_type = parsed_picture_header.inter_slice_allowed_flag
                           ? static_cast<int>(reader.uvlc())
                           : I_SLICE;
  if (summary.slice_type < B_SLICE || summary.slice_type > I_SLICE) {
    std::ostringstream oss;
    oss << "Unsupported VVC slice_type " << summary.slice_type;
    setError(oss.str());
    return -1;
  }
  if (isIrayOrGdrNal(nalu.nal_unit_type)) {
    summary.no_output_of_prior_pics_flag = reader.flag() != 0;
  }

  const int max_poc_lsb = 1 << sps->bits_for_poc;
  if (isIdrNal(nalu.nal_unit_type)) {
    const int poc_msb =
        parsed_picture_header.poc_msb_present_flag
            ? parsed_picture_header.poc_msb_val * max_poc_lsb
            : 0;
    parsed_picture_header.poc = poc_msb + parsed_picture_header.poc_lsb;
  } else {
    const int prev_poc_lsb = prev_tid0_poc & (max_poc_lsb - 1);
    const int prev_poc_msb = prev_tid0_poc - prev_poc_lsb;
    int poc_msb = 0;

    if (parsed_picture_header.poc_msb_present_flag) {
      poc_msb = parsed_picture_header.poc_msb_val * max_poc_lsb;
    } else if ((parsed_picture_header.poc_lsb < prev_poc_lsb) &&
               ((prev_poc_lsb - parsed_picture_header.poc_lsb) >=
                (max_poc_lsb / 2))) {
      poc_msb = prev_poc_msb + max_poc_lsb;
    } else if ((parsed_picture_header.poc_lsb > prev_poc_lsb) &&
               ((parsed_picture_header.poc_lsb - prev_poc_lsb) >
                (max_poc_lsb / 2))) {
      poc_msb = prev_poc_msb - max_poc_lsb;
    } else {
      poc_msb = prev_poc_msb;
    }

    parsed_picture_header.poc = poc_msb + parsed_picture_header.poc_lsb;
  }

  if (!parseSliceHeaderTail(reader, nalu, *sps, *pps, parsed_picture_header,
                            summary, m_last_error)) {
    return -1;
  }

  parsed_picture_header.valid = true;

  summary.valid = true;
  summary.nal_unit_type = nalu.nal_unit_type;
  summary.temporal_id = nalu.TemporalId;
  summary.pps_id = parsed_picture_header.pps_id;
  summary.sps_id = parsed_picture_header.sps_id;
  summary.width = pps->pic_width_in_luma_samples;
  summary.height = pps->pic_height_in_luma_samples;
  summary.poc_lsb = parsed_picture_header.poc_lsb;
  summary.poc = parsed_picture_header.poc;
  summary.gdr_or_irap_pic_flag = parsed_picture_header.gdr_or_irap_pic_flag;
  summary.non_ref_pic_flag = parsed_picture_header.non_ref_pic_flag;
  summary.inter_slice_allowed_flag =
      parsed_picture_header.inter_slice_allowed_flag;
  summary.intra_slice_allowed_flag =
      parsed_picture_header.intra_slice_allowed_flag;

  return 0;
}

const char *VvcMiniParser::sliceTypeToString(int slice_type) {
  switch (slice_type) {
  case B_SLICE:
    return "B";
  case P_SLICE:
    return "P";
  case I_SLICE:
    return "I";
  default:
    return "UNK";
  }
}
