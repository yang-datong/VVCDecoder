#ifndef VVC_MINI_PARSER_HPP_9RJ3ZQ2L
#define VVC_MINI_PARSER_HPP_9RJ3ZQ2L

#include "BitStream.hpp"
#include "Nalu.hpp"
#include <array>
#include <string>
#include <vector>

struct VvcSpsState {
  bool valid = false;
  int sps_id = -1;
  int max_sublayers_minus1 = 0;
  int chroma_format_idc = 0;
  int ctu_size = 0;
  int log2_min_cb_size = 2;
  int max_pic_width_in_luma_samples = 0;
  int max_pic_height_in_luma_samples = 0;
  int bits_for_poc = 8;
  bool poc_msb_cycle_flag = false;
  int poc_msb_cycle_len_minus1 = 0;
  bool subpic_info_present_flag = false;
  int subpic_id_len = 0;
  int num_extra_ph_bytes = 0;
  int num_extra_sh_bytes = 0;
  std::vector<bool> extra_ph_bit_present_flags;
  std::vector<bool> extra_sh_bit_present_flags;
  int bit_depth = 8;
  int qp_bd_offset = 0;
  int max_tb_size = 32;
  int log2_max_transform_skip_block_size = 2;
  std::array<int, 3> min_qt_sizes = {};
  std::array<int, 3> max_mtt_depths = {};
  std::array<int, 3> max_bt_sizes = {};
  std::array<int, 3> max_tt_sizes = {};

  bool split_cons_override_enabled_flag = false;
  bool dual_tree_intra_flag = false;
  bool entropy_coding_sync_enabled_flag = false;
  bool entry_point_offsets_present_flag = false;
  bool joint_cbcr_enabled_flag = false;
  bool use_sao = false;
  bool use_alf = false;
  bool use_ccalf = false;
  bool use_lmcs = false;
  bool weighted_pred_flag = false;
  bool weighted_bipred_flag = false;
  bool long_term_ref_pics_flag = false;
  bool idr_rpl_present_flag = false;
  bool temporal_mvp_enabled_flag = false;
  bool sbtmvp_enabled_flag = false;
  bool bdof_enabled_flag = false;
  bool bdof_control_present_in_ph_flag = false;
  bool dmvr_enabled_flag = false;
  bool dmvr_control_present_in_ph_flag = false;
  bool mmvd_enabled_flag = false;
  bool mmvd_fullpel_only_enabled_flag = false;
  bool affine_enabled_flag = false;
  bool prof_control_present_in_ph_flag = false;
  bool transform_skip_enabled_flag = false;
  bool bdpcm_enabled_flag = false;
  bool mts_enabled_flag = false;
  bool lfnst_enabled_flag = false;
  bool isp_enabled_flag = false;
  bool mrl_enabled_flag = false;
  bool mip_enabled_flag = false;
  bool cclm_enabled_flag = false;
  bool scaling_list_enabled_flag = false;
  bool dep_quant_enabled_flag = false;
  bool sign_data_hiding_enabled_flag = false;
  bool virtual_boundaries_enabled_flag = false;
  bool virtual_boundaries_present_flag = false;
};

struct VvcPpsState {
  bool valid = false;
  int pps_id = -1;
  int sps_id = -1;
  int pic_width_in_luma_samples = 0;
  int pic_height_in_luma_samples = 0;
  bool output_flag_present_flag = false;
  bool no_pic_partition_flag = false;
  bool cabac_init_present_flag = false;
  bool rpl1_idx_present_flag = false;
  bool weighted_pred_flag = false;
  bool weighted_bipred_flag = false;
  int num_ref_idx_l0_default_active = 1;
  int num_ref_idx_l1_default_active = 1;
  int init_qp_minus26 = 0;
  bool cu_qp_delta_enabled_flag = false;
  bool chroma_tool_offsets_present_flag = false;
  bool joint_cbcr_qp_offset_present_flag = false;
  bool slice_chroma_qp_offsets_present_flag = false;
  bool cu_chroma_qp_offset_list_enabled_flag = false;
  bool deblocking_filter_control_present_flag = false;
  bool deblocking_filter_override_enabled_flag = false;
  bool pps_deblocking_filter_disabled_flag = false;
  bool dbf_info_in_ph_flag = false;
  bool rpl_info_in_ph_flag = false;
  bool sao_info_in_ph_flag = false;
  bool alf_info_in_ph_flag = false;
  bool wp_info_in_ph_flag = false;
  bool qp_delta_info_in_ph_flag = false;
  bool picture_header_extension_present_flag = false;
  bool slice_header_extension_present_flag = false;
};

struct VvcPictureHeaderState {
  bool valid = false;
  bool in_slice_header = false;
  bool gdr_or_irap_pic_flag = false;
  bool non_ref_pic_flag = false;
  bool gdr_pic_flag = false;
  bool inter_slice_allowed_flag = false;
  bool intra_slice_allowed_flag = true;
  int pps_id = -1;
  int sps_id = -1;
  int poc_lsb = 0;
  bool poc_msb_present_flag = false;
  int poc_msb_val = 0;
  bool partition_constraints_override_flag = false;
  std::array<int, 3> min_qt_sizes = {};
  std::array<int, 3> max_mtt_depths = {};
  std::array<int, 3> max_bt_sizes = {};
  std::array<int, 3> max_tt_sizes = {};
  int cu_qp_delta_subdiv_intra_slice = 0;
  int cu_qp_delta_subdiv_inter_slice = 0;
  bool temporal_mvp_enabled_flag = false;
  bool mmvd_fullpel_only_flag = false;
  bool mvd_l1_zero_flag = true;
  bool bdof_disabled_flag = true;
  bool dmvr_disabled_flag = true;
  bool prof_disabled_flag = true;
  bool joint_cbcr_sign_flag = false;
  int poc = 0;
};

struct VvcFrameHeaderSummary {
  bool valid = false;
  int nal_unit_type = 0;
  int temporal_id = 0;
  bool picture_header_in_slice_header_flag = false;
  int pps_id = -1;
  int sps_id = -1;
  int width = 0;
  int height = 0;
  int poc_lsb = 0;
  int poc = 0;
  int slice_type = I_SLICE;
  bool no_output_of_prior_pics_flag = false;
  bool gdr_or_irap_pic_flag = false;
  bool non_ref_pic_flag = false;
  bool inter_slice_allowed_flag = false;
  bool intra_slice_allowed_flag = true;
  bool alf_enabled_flag = false;
  int num_alf_aps_ids_luma = 0;
  bool alf_cb_enabled_flag = false;
  bool alf_cr_enabled_flag = false;
  bool alf_cc_cb_enabled_flag = false;
  bool alf_cc_cr_enabled_flag = false;
  bool sao_luma_used_flag = false;
  bool sao_chroma_used_flag = false;
  bool dep_quant_used_flag = false;
  bool sign_data_hiding_used_flag = false;
  bool ts_residual_coding_disabled_flag = false;
  int slice_qp_delta = 0;
  int slice_qp_y = 26;
  int payload_rbsp_bit_offset = -1;
  int payload_rbsp_byte_offset = -1;
  int payload_bit_offset = -1;
  int payload_byte_offset = -1;
};

class VvcMiniParser {
 public:
  int parseSps(BitStream &bs, VvcSpsState &sps);
  int parsePps(BitStream &bs, const std::array<VvcSpsState, 16> &spss,
               VvcPpsState &pps);
  int parsePictureHeader(BitStream &bs, const std::array<VvcSpsState, 16> &spss,
                         const std::array<VvcPpsState, 64> &ppss,
                         VvcPictureHeaderState &picture_header);
  int parseSliceHeader(const Nalu &nalu, BitStream &bs,
                       const std::array<VvcSpsState, 16> &spss,
                       const std::array<VvcPpsState, 64> &ppss,
                       int prev_tid0_poc,
                       const VvcPictureHeaderState *active_picture_header,
                       VvcPictureHeaderState &parsed_picture_header,
                       VvcFrameHeaderSummary &summary);

  const std::string &lastError() const { return m_last_error; }

  static const char *sliceTypeToString(int slice_type);

 private:
  std::string m_last_error;

  void setError(const std::string &error) { m_last_error = error; }
  static bool isIrayOrGdrNal(uint8_t nal_unit_type);
  static bool isIdrNal(uint8_t nal_unit_type);
};

#endif
