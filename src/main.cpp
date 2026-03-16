#include "AnnexBReader.hpp"
#include "Frame.hpp"
#include "GOP.hpp"
#include "Image.hpp"
#include "Nalu.hpp"
#include "VvcCabacReader.hpp"
#include "VvcCtuPrefixProbe.hpp"
#include "VvcIntraCuProbe.hpp"
#include "VvcMiniParser.hpp"
#include "VvcReconstructionProbe.hpp"
#include "VvcResidualProbe.hpp"
#include "VvcSplitProbe.hpp"
#include "VvcTransformTreeProbe.hpp"
#include <array>
#include <cstdlib>
#include <iomanip>

typedef enum _OUTPUT_FILE_TYPE {
  OUTPUT_NONE,
  OUTPUT_BMP,
  OUTPUT_YUV
} OUTPUT_FILE_TYPE;

int32_t g_output_width = 0;
int32_t g_output_height = 0;
int32_t g_vvc_picture_counter = 0;
int32_t g_output_file_type = OUTPUT_NONE;
bool g_first_frame_saved = false;

static const char *findMaxVclEnv() {
  const char *value = std::getenv("VVC_MAX_VCL");
  if (value && *value) return value;
  return std::getenv("HEVC_MAX_VCL");
}

static int findPayloadRbspDeltaEnv() {
  const char *value = std::getenv("VVC_PAYLOAD_RBSP_DELTA");
  if (value && *value) return std::atoi(value);
  return 0;
}

struct VvcBitstreamSummary {
  int total_nalus = 0;
  int vcl_nalus = 0;
  int non_vcl_nalus = 0;
  int parsed_sps = 0;
  int parsed_pps = 0;
  int parsed_picture_headers = 0;
  int parsed_slice_headers = 0;
  int parsed_pictures = 0;
  std::array<int, VVC_NAL_UNIT_INVALID + 1> type_counts = {};
};

static bool isVvcBitstreamPath(const string &filePath) {
  return filePath.size() >= 5 &&
         (filePath.substr(filePath.size() - 5) == ".h266" ||
          filePath.substr(filePath.size() - 4) == ".vvc");
}

static int parseVvcBitstream(const string &filePath) {
  string readerPath = filePath;
  AnnexBReader reader(readerPath);
  int result = reader.open();
  RET(result);

  VvcBitstreamSummary summary;
  VvcMiniParser parser;
  std::array<VvcSpsState, 16> spss = {};
  std::array<VvcPpsState, 64> ppss = {};
  VvcPictureHeaderState active_picture_header;
  int prev_tid0_poc = 0;
  bool prev_tid0_poc_valid = false;
  int last_picture_poc = 0;
  bool last_picture_poc_valid = false;
  int number = 0;
  int parsed_vcl_count = 0;
  int max_parsed_vcl = 0;
  const int payload_rbsp_delta = findPayloadRbspDeltaEnv();
  if (const char *max_vcl_env = findMaxVclEnv()) {
    const int parsed = std::atoi(max_vcl_env);
    if (parsed > 0) max_parsed_vcl = parsed;
  }

  while (true) {
    Nalu nalu;
    Nalu::EBSP ebsp;
    Nalu::RBSP rbsp;

    result = reader.readNalu(nalu);
    if (result != 0 && result != 1) {
      reader.close();
      return -1;
    }

    RET(nalu.parseEBSP(ebsp));
    RET(nalu.parseVvcRBSP(ebsp, rbsp));

    const bool is_vcl = Nalu::isVvcVclNaluType(nalu.nal_unit_type);
    summary.total_nalus++;
    if (is_vcl)
      summary.vcl_nalus++;
    else
      summary.non_vcl_nalus++;

    if (nalu.nal_unit_type <= VVC_NAL_UNIT_INVALID)
      summary.type_counts[nalu.nal_unit_type]++;
    else
      summary.type_counts[VVC_NAL_UNIT_INVALID]++;

    cout << "NAL[" << std::setw(2) << ++number << "] "
         << Nalu::vvcNalUnitTypeToString(nalu.nal_unit_type) << "("
         << static_cast<int>(nalu.nal_unit_type) << ")"
         << " class=" << (is_vcl ? "VCL" : "Non-VCL")
         << " layer=" << static_cast<int>(nalu.nuh_layer_id)
         << " tid=" << static_cast<int>(nalu.TemporalId)
         << " start_code=" << nalu.startCodeLenth << " ebsp=" << ebsp.len
         << " rbsp=" << rbsp.len;

    if (nalu.forbidden_zero_bit != 0 || nalu.nuh_reserved_zero_bit != 0 ||
        nalu.nuh_temporal_id_plus1 == 0) {
      cout << " header_warning";
    }

    BitStream bitStream(rbsp.buf, rbsp.len);
    switch (nalu.nal_unit_type) {
    case VVC_NAL_UNIT_SPS: {
      VvcSpsState sps;
      if (parser.parseSps(bitStream, sps) != 0) {
        cerr << "VVC SPS parse error on NAL[" << number
             << "]: " << parser.lastError() << endl;
        reader.close();
        return -1;
      }
      spss[sps.sps_id] = sps;
      summary.parsed_sps++;
      cout << " sps_id=" << sps.sps_id << " max=" << sps.max_pic_width_in_luma_samples
           << "x" << sps.max_pic_height_in_luma_samples
           << " ctu=" << sps.ctu_size << " poc_bits=" << sps.bits_for_poc
           << " chroma=" << sps.chroma_format_idc
           << " min_qt_i=" << sps.min_qt_sizes[0]
           << " max_mtt_i=" << sps.max_mtt_depths[0]
           << " lfnst=" << static_cast<int>(sps.lfnst_enabled_flag)
           << " mip=" << static_cast<int>(sps.mip_enabled_flag)
           << " mrl=" << static_cast<int>(sps.mrl_enabled_flag)
           << " isp=" << static_cast<int>(sps.isp_enabled_flag);
      break;
    }
    case VVC_NAL_UNIT_PPS: {
      VvcPpsState pps;
      if (parser.parsePps(bitStream, spss, pps) != 0) {
        cerr << "VVC PPS parse error on NAL[" << number
             << "]: " << parser.lastError() << endl;
        reader.close();
        return -1;
      }
      ppss[pps.pps_id] = pps;
      summary.parsed_pps++;
      cout << " pps_id=" << pps.pps_id << " sps_id=" << pps.sps_id
           << " pic=" << pps.pic_width_in_luma_samples << "x"
           << pps.pic_height_in_luma_samples
           << " no_pic_partition=" << static_cast<int>(pps.no_pic_partition_flag);
      break;
    }
    case VVC_NAL_UNIT_PH: {
      VvcPictureHeaderState picture_header;
      if (parser.parsePictureHeader(bitStream, spss, ppss, picture_header) != 0) {
        cerr << "VVC picture header parse error on NAL[" << number
             << "]: " << parser.lastError() << endl;
        reader.close();
        return -1;
      }
      active_picture_header = picture_header;
      summary.parsed_picture_headers++;
      cout << " ph pps_id=" << picture_header.pps_id
           << " sps_id=" << picture_header.sps_id
           << " poc_lsb=" << picture_header.poc_lsb
           << " inter=" << static_cast<int>(picture_header.inter_slice_allowed_flag)
           << " intra=" << static_cast<int>(picture_header.intra_slice_allowed_flag);
      break;
    }
    default:
      if (Nalu::isVvcSliceNaluType(nalu.nal_unit_type)) {
        VvcPictureHeaderState parsed_picture_header;
        VvcFrameHeaderSummary frame_summary;
        if (parser.parseSliceHeader(
                nalu, bitStream, spss, ppss,
                prev_tid0_poc_valid ? prev_tid0_poc : 0,
                active_picture_header.valid ? &active_picture_header : nullptr,
                parsed_picture_header, frame_summary) != 0) {
          cerr << "VVC slice header parse error on NAL[" << number
               << "]: " << parser.lastError() << endl;
          reader.close();
          return -1;
        }

        active_picture_header = parsed_picture_header;
        summary.parsed_slice_headers++;
        parsed_vcl_count++;

        if (!last_picture_poc_valid || frame_summary.poc != last_picture_poc) {
          last_picture_poc = frame_summary.poc;
          last_picture_poc_valid = true;
          summary.parsed_pictures++;
        }

        if (nalu.TemporalId == 0) {
          prev_tid0_poc = frame_summary.poc;
          prev_tid0_poc_valid = true;
        }

        const int cabac_payload_rbsp_offset =
            frame_summary.payload_rbsp_byte_offset + payload_rbsp_delta;
        if (cabac_payload_rbsp_offset < 0 ||
            cabac_payload_rbsp_offset >= rbsp.len) {
          cerr << "VVC CABAC payload offset error on NAL[" << number << "]"
               << ": rbsp payload offset="
               << cabac_payload_rbsp_offset
               << ", rbsp len=" << rbsp.len << endl;
          reader.close();
          return -1;
        }

        VvcCabacReader cabac_reader;
        const uint8_t *cabac_payload = rbsp.buf + cabac_payload_rbsp_offset;
        const int cabac_payload_len = rbsp.len - cabac_payload_rbsp_offset;
        if (cabac_reader.init(cabac_payload, cabac_payload_len) != 0) {
          cerr << "VVC CABAC init error on NAL[" << number
               << "]: " << cabac_reader.lastError() << endl;
          reader.close();
          return -1;
        }

        VvcSplitProbeResult split_probe_result;
        bool split_probe_ok = false;
        std::string split_probe_error;
        VvcCtuPrefixProbeResult ctu_prefix_result;
        bool ctu_prefix_ok = false;
        std::string ctu_prefix_error;
        VvcIntraCuProbeResult intra_probe_result;
        bool intra_probe_ok = false;
        std::string intra_probe_error;
        VvcTransformTreeProbeResult transform_probe_result;
        bool transform_probe_ok = false;
        std::string transform_probe_error;
        VvcResidualProbeResult residual_probe_result;
        bool residual_probe_ok = false;
        std::string residual_probe_error;
        VvcReconstructionProbeResult reconstruction_probe_result;
        bool reconstruction_probe_ok = false;
        std::string reconstruction_probe_error;
        if (frame_summary.slice_type == I_SLICE &&
            frame_summary.sps_id >= 0 &&
            frame_summary.sps_id < static_cast<int>(spss.size()) &&
            spss[frame_summary.sps_id].valid) {
          VvcCtuPrefixProbe ctu_prefix_probe;
          if (ctu_prefix_probe.consumeFirstCtuPrefix(
                  cabac_reader, spss[frame_summary.sps_id],
                  frame_summary.slice_type, frame_summary.slice_qp_y,
                  frame_summary, ctu_prefix_result) == 0) {
            ctu_prefix_ok = ctu_prefix_result.valid;
            if (ctu_prefix_ok) {
              VvcSplitProbe split_probe;
              if (split_probe.probeFirstCuSplitPath(
                      cabac_reader, spss[frame_summary.sps_id],
                      parsed_picture_header, frame_summary.slice_type,
                      frame_summary.slice_qp_y, split_probe_result) == 0) {
                split_probe_ok = split_probe_result.valid;
                if (split_probe_ok) {
                  VvcIntraCuProbe intra_probe;
                  if (intra_probe.probeFirstCuIntraSyntax(
                          cabac_reader, spss[frame_summary.sps_id],
                          frame_summary.slice_type, frame_summary.slice_qp_y,
                          split_probe_result, intra_probe_result) == 0) {
                    intra_probe_ok = intra_probe_result.valid;
                    if (intra_probe_ok) {
                      VvcTransformTreeProbe transform_probe;
                      if (transform_probe.probeFirstTransformUnit(
                              cabac_reader, spss[frame_summary.sps_id],
                              frame_summary.slice_type, frame_summary.slice_qp_y,
                              split_probe_result, intra_probe_result,
                              transform_probe_result) == 0) {
                        transform_probe_ok = transform_probe_result.valid;
                        if (transform_probe_ok) {
                          VvcResidualProbe residual_probe;
                          if (residual_probe.probeFirstTuResidualSyntax(
                                  cabac_reader, spss[frame_summary.sps_id],
                                  frame_summary.slice_type,
                                  frame_summary.slice_qp_y,
                                  frame_summary.dep_quant_used_flag,
                                  frame_summary.sign_data_hiding_used_flag,
                                  transform_probe_result,
                                  residual_probe_result) == 0) {
                            residual_probe_ok = residual_probe_result.valid;
                            if (residual_probe_ok) {
                              VvcReconstructionProbe reconstruction_probe;
                              if (reconstruction_probe
                                      .reconstructFirstLumaTransformBlock(
                                          spss[frame_summary.sps_id],
                                          frame_summary.slice_qp_y,
                                          frame_summary.dep_quant_used_flag,
                                          intra_probe_result,
                                          transform_probe_result,
                                          residual_probe_result,
                                          reconstruction_probe_result) == 0) {
                                reconstruction_probe_ok =
                                    reconstruction_probe_result.valid;
                              } else {
                                reconstruction_probe_error =
                                    reconstruction_probe.lastError();
                              }
                            }
                          } else {
                            residual_probe_error = residual_probe.lastError();
                          }
                        }
                      } else {
                        transform_probe_error = transform_probe.lastError();
                      }
                    }
                  } else {
                    intra_probe_error = intra_probe.lastError();
                  }
                }
              } else {
                split_probe_error = split_probe.lastError();
              }
            }
          } else {
            ctu_prefix_error = ctu_prefix_probe.lastError();
          }
          if (!ctu_prefix_ok && ctu_prefix_error.empty()) {
            ctu_prefix_error = "First-CTU prefix probe did not produce a valid result";
          }
        }

        cout << " pps_id=" << frame_summary.pps_id
             << " sps_id=" << frame_summary.sps_id
             << " pic=" << frame_summary.width << "x" << frame_summary.height
             << " ph_in_sh="
             << static_cast<int>(frame_summary.picture_header_in_slice_header_flag)
             << " poc_lsb=" << frame_summary.poc_lsb
             << " poc=" << frame_summary.poc
             << " slice=" << VvcMiniParser::sliceTypeToString(frame_summary.slice_type)
             << " qp=" << frame_summary.slice_qp_y
             << " depq=" << static_cast<int>(frame_summary.dep_quant_used_flag)
             << " inter="
             << static_cast<int>(frame_summary.inter_slice_allowed_flag)
             << " intra="
             << static_cast<int>(frame_summary.intra_slice_allowed_flag)
             << " noprior="
             << static_cast<int>(frame_summary.no_output_of_prior_pics_flag)
             << " payload_byte=" << frame_summary.payload_byte_offset
             << " payload_rbsp=" << frame_summary.payload_rbsp_byte_offset;
        if (payload_rbsp_delta != 0) {
          cout << " payload_rbsp_delta=" << payload_rbsp_delta
               << " payload_rbsp_used=" << cabac_payload_rbsp_offset;
        }
        if (ctu_prefix_ok) {
          cout << " first_ctu_prefix_bins=" << ctu_prefix_result.bins_consumed
               << " first_ctu_sao_bins=" << ctu_prefix_result.sao_bins
               << " first_ctu_alf_bins=" << ctu_prefix_result.alf_bins
               << " first_ctu_sao="
               << static_cast<int>(ctu_prefix_result.sao_consumed)
               << " first_ctu_sao_y=" << ctu_prefix_result.sao_luma_mode
               << " first_ctu_sao_c=" << ctu_prefix_result.sao_chroma_mode
               << " first_ctu_sao_y_off={"
               << ctu_prefix_result.sao_luma_offsets[0] << ","
               << ctu_prefix_result.sao_luma_offsets[1] << ","
               << ctu_prefix_result.sao_luma_offsets[2] << ","
               << ctu_prefix_result.sao_luma_offsets[3] << "}"
               << " first_ctu_sao_y_band="
               << ctu_prefix_result.sao_luma_band_position
               << " first_ctu_alf="
               << static_cast<int>(ctu_prefix_result.alf_consumed)
               << " first_ctu_alf_y="
               << static_cast<int>(ctu_prefix_result.alf_luma_enabled)
               << " first_ctu_alf_t="
               << static_cast<int>(ctu_prefix_result.alf_use_temporal);
        } else if (!ctu_prefix_error.empty()) {
          cout << " ctu_prefix_err=" << ctu_prefix_error;
        }
        if (split_probe_ok) {
          cout << " split_path=" << split_probe_result.path
               << " split_leaf=" << split_probe_result.leaf_width << "x"
               << split_probe_result.leaf_height
               << " split_bins=" << split_probe_result.decisions;
          if (intra_probe_ok) {
            cout << " first_cu_luma=" << intra_probe_result.intra_luma_mode
                 << " first_cu_src=" << intra_probe_result.intra_luma_source
                 << " first_cu_mip=" << static_cast<int>(intra_probe_result.mip_flag)
                 << " first_cu_mrl=" << intra_probe_result.multi_ref_idx
                 << " first_cu_isp=" << intra_probe_result.isp_mode;
            if (intra_probe_result.mip_flag) {
              cout << " first_cu_mip_tr="
                   << static_cast<int>(intra_probe_result.mip_transposed_flag);
            } else {
              cout << " first_cu_mpm={"
                   << intra_probe_result.mpm_candidates[0] << ","
                   << intra_probe_result.mpm_candidates[1] << ","
                   << intra_probe_result.mpm_candidates[2] << ","
                   << intra_probe_result.mpm_candidates[3] << ","
                   << intra_probe_result.mpm_candidates[4] << ","
                   << intra_probe_result.mpm_candidates[5] << "}";
            }
            if (transform_probe_ok) {
              cout << " first_tu=" << transform_probe_result.first_tu_width
                   << "x" << transform_probe_result.first_tu_height
                   << " first_tu_depth=" << transform_probe_result.first_tu_depth
                   << " first_tu_cbf_y=" << transform_probe_result.first_tu_cbf_y
                   << " first_tu_cbf_cb=" << transform_probe_result.first_tu_cbf_cb
                   << " first_tu_cbf_cr=" << transform_probe_result.first_tu_cbf_cr;
              if (transform_probe_result.first_tu_ts_flag_coded) {
                cout << " first_tu_ts=" << transform_probe_result.first_tu_ts_flag;
              }
              if (transform_probe_result.first_tu_last_sig_x >= 0 &&
                  transform_probe_result.first_tu_last_sig_y >= 0) {
                cout << " first_tu_last_sig=("
                     << transform_probe_result.first_tu_last_sig_x << ","
                     << transform_probe_result.first_tu_last_sig_y << ")";
              }
              if (residual_probe_ok) {
                cout << " first_tu_scan_pos_last="
                     << residual_probe_result.scan_pos_last
                     << " first_tu_last_subset="
                     << residual_probe_result.last_subset_id
                     << " first_tu_sig_groups="
                     << residual_probe_result.sig_group_count
                     << " first_tu_first_sig_subset="
                     << residual_probe_result.first_sig_subset_id
                     << " first_tu_nonzero="
                     << residual_probe_result.non_zero_coeffs
                     << " first_tu_abs_sum="
                     << residual_probe_result.abs_sum
                     << " first_tu_max_abs="
                     << residual_probe_result.max_abs_level
                     << " first_tu_last_coeff="
                     << residual_probe_result.last_coeff_value;
                if (reconstruction_probe_ok) {
                  cout << " first_tu_pred_dc="
                       << reconstruction_probe_result.pred_dc
                       << " first_tu_resi_min="
                       << reconstruction_probe_result.residual_min
                       << " first_tu_resi_max="
                       << reconstruction_probe_result.residual_max
                       << " first_tu_recon_min="
                       << reconstruction_probe_result.recon_min
                       << " first_tu_recon_max="
                       << reconstruction_probe_result.recon_max
                       << " first_tu_recon_sum="
                       << reconstruction_probe_result.recon_sum
                       << " first_tu_recon_tl="
                       << reconstruction_probe_result.recon_top_left
                       << " first_tu_recon_c="
                       << reconstruction_probe_result.recon_center
                       << " first_tu_recon_br="
                       << reconstruction_probe_result.recon_bottom_right;
                } else if (!reconstruction_probe_error.empty()) {
                  cout << " recon_probe_err=" << reconstruction_probe_error;
                }
              } else if (!residual_probe_error.empty()) {
                cout << " residual_probe_err=" << residual_probe_error;
              }
            } else if (!transform_probe_error.empty()) {
              cout << " transform_probe_err=" << transform_probe_error;
            }
          } else if (!intra_probe_error.empty()) {
            cout << " intra_probe_err=" << intra_probe_error;
          }
        } else if (!split_probe_error.empty()) {
          cout << " split_probe_err=" << split_probe_error;
        }
      }
      break;
    }
    cout << endl;

    if (result == 0) break;
    if (max_parsed_vcl > 0 && parsed_vcl_count >= max_parsed_vcl) break;
  }

  cout << endl;
  cout << "VVC bitstream summary" << endl;
  cout << "  total_nalus: " << summary.total_nalus << endl;
  cout << "  vcl_nalus: " << summary.vcl_nalus << endl;
  cout << "  non_vcl_nalus: " << summary.non_vcl_nalus << endl;
  cout << "  parsed_sps: " << summary.parsed_sps << endl;
  cout << "  parsed_pps: " << summary.parsed_pps << endl;
  cout << "  parsed_picture_headers: " << summary.parsed_picture_headers
       << endl;
  cout << "  parsed_slice_headers: " << summary.parsed_slice_headers << endl;
  cout << "  parsed_pictures: " << summary.parsed_pictures << endl;
  cout << "  type_histogram:" << endl;
  for (size_t i = 0; i < summary.type_counts.size(); ++i) {
    if (summary.type_counts[i] == 0) continue;
    cout << "    " << Nalu::vvcNalUnitTypeToString(static_cast<uint8_t>(i))
         << "(" << i << "): " << summary.type_counts[i] << endl;
  }

  reader.close();
  return 0;
}

static int outputDecodedFrame(GOP *gop, Frame *frame);
static int flushDecodedFrame(GOP *gop, Frame *&frame, bool is_from_idr);

int main(int argc, char *argv[]) {
  string filePath;
  if (argc > 1 && argv[1] != nullptr)
    filePath = argv[1];
  else {
    filePath = "./demo.h266";
  }

  if (isVvcBitstreamPath(filePath)) return parseVvcBitstream(filePath);

  AnnexBReader reader(filePath);
  int result = reader.open();
  RET(result);

  GOP *gop = new GOP();
  Frame *frame = gop->m_dpb[0];

  BitStream *bitStream = nullptr;

  int number = 0;
  int decoded_vcl_count = 0;
  int max_decoded_vcl = 1;
  if (const char *max_vcl_env = findMaxVclEnv()) {
    const int parsed = std::atoi(max_vcl_env);
    if (parsed > 0) max_decoded_vcl = parsed;
  }

  while (true) {
    Nalu nalu;
    Nalu::EBSP ebsp;
    Nalu::RBSP rbsp;
    SEI sei;
    result = reader.readNalu(nalu);

    if (result == 1 || result == 0) {
      cout << "Reading a NAL[" << ++number << "]{" << (int)nalu.buffer[0] << " "
           << (int)nalu.buffer[1] << " " << (int)nalu.buffer[2] << " "
           << (int)nalu.buffer[3] << "}, Buffer len[" << nalu.len << "]";

      nalu.parseEBSP(ebsp);
      cout << "   --->   EBSP[" << number << "]{" << (int)ebsp.buf[0] << " "
           << (int)ebsp.buf[1] << " " << (int)ebsp.buf[2] << " "
           << (int)ebsp.buf[3] << "}, Buffer len[" << ebsp.len << "]";

      nalu.parseRBSP(ebsp, rbsp);
      cout << "  --->   RBSP[" << number << "]{" << (int)rbsp.buf[0] << " "
           << (int)rbsp.buf[1] << " " << (int)rbsp.buf[2] << " "
           << (int)rbsp.buf[3] << "}, Buffer len[" << rbsp.len << "]" << endl;

      // nalu.parseSODB(rbsp, SODB);

      if (nalu.nal_unit_type > 40) cout << "Unknown Nalu Type !!!" << endl;

      switch (nalu.nal_unit_type) {
      case HEVC_NAL_VPS:
        cout << "VPS -> {" << endl;
        nalu.extractVPSparameters(rbsp, gop->m_vpss, gop->last_vps_id);
        cout << " }" << endl;
        break;
      case HEVC_NAL_SPS:
        cout << "SPS -> {" << endl;
        nalu.extractSPSparameters(rbsp, gop->m_spss, gop->last_sps_id,
                                  gop->m_vpss);
        cout << " }" << endl;

        break;
      case HEVC_NAL_PPS:
        cout << "PPS -> {" << endl;
        nalu.extractPPSparameters(
            rbsp, gop->m_ppss, gop->last_pps_id,
            gop->m_spss[gop->last_sps_id].chroma_format_idc, gop->m_spss);
        cout << " }" << endl;

        break;
      case HEVC_NAL_SEI_PREFIX:
      case HEVC_NAL_SEI_SUFFIX:
        cout << "SEI -> {" << endl;
        nalu.extractSEIparameters(rbsp, sei, gop->m_spss[gop->last_pps_id]);
        cout << " }" << endl;
        break;
      case HEVC_NAL_TRAIL_R:
      case HEVC_NAL_TRAIL_N:
      case HEVC_NAL_TSA_N:
      case HEVC_NAL_TSA_R:
      case HEVC_NAL_STSA_N:
      case HEVC_NAL_STSA_R:
      case HEVC_NAL_BLA_W_LP:
      case HEVC_NAL_BLA_W_RADL:
      case HEVC_NAL_BLA_N_LP:
      case HEVC_NAL_IDR_W_RADL:
      case HEVC_NAL_IDR_N_LP:
      case HEVC_NAL_CRA_NUT:
      case HEVC_NAL_RADL_N:
      case HEVC_NAL_RADL_R:
      case HEVC_NAL_RASL_N:
      case HEVC_NAL_RASL_R:
        cout << "Original Slice -> {" << endl;
        std::cout << (int)nalu.nal_unit_type << std::endl;
        flushDecodedFrame(gop, frame, false);
        delete bitStream;
        bitStream = new BitStream(rbsp.buf, rbsp.len);
        nalu.extractSliceparameters(*bitStream, *gop, *frame);
        frame->decode(*bitStream, gop->m_dpb, *gop);
        decoded_vcl_count++;
        cout << " }" << endl;
        if (decoded_vcl_count >= max_decoded_vcl) {
          result = 0;
        }
        break;
      case HEVC_NAL_EOS_NUT:
      case HEVC_NAL_EOB_NUT:
      case HEVC_NAL_AUD:
      case HEVC_NAL_FD_NUT:
        std::cout << "HEVC_NAL_EOS_NUT" << std::endl;
        break;
      default:
        cerr << "Skip nal_unit_type:" << (int)nalu.nal_unit_type << endl;
      }

      if (result == 0 || decoded_vcl_count >= max_decoded_vcl) break;
    } else {
      RET(-1);
      break;
    }
    cout << endl;
  }

  flushDecodedFrame(gop, frame, true);

  for (int i = 0; i < gop->m_max_num_reorder_frames; ++i)
    outputDecodedFrame(gop, nullptr);

  if (g_output_file_type == OUTPUT_YUV)
    cout << "\tffplay -video_size " << g_output_width << "x"
         << g_output_height
         << " output.yuv" << endl;

  reader.close();
  delete gop;
  delete bitStream;
  return 0;
}

int flushDecodedFrame(GOP *gop, Frame *&frame, bool is_from_idr) {
  if (frame != nullptr && frame->m_current_picture_ptr != nullptr &&
      frame->m_picture_frame.m_slice != nullptr) {
    Frame *newEmptyPicture = nullptr;
    frame->m_current_picture_ptr->getEmptyFrameFromDPB(newEmptyPicture);

    if (is_from_idr == false)
      if (frame->m_picture_frame.m_slice->slice_header->IdrPicFlag) {
        g_output_width = frame->m_picture_frame.PicWidthInSamplesL;
        g_output_height = frame->m_picture_frame.PicHeightInSamplesL;
        gop->flush();
      }

    outputDecodedFrame(gop, frame);
    frame = newEmptyPicture;
  }
  return 0;
}

int outputDecodedFrame(GOP *gop, Frame *frame) {
  Frame *outPicture = nullptr;
  gop->outputOneFrame(frame, outPicture);
  if (outPicture != nullptr) {
    static int index = 0;
    outPicture->m_is_in_use = false;
    Image image;
    if (!g_first_frame_saved) {
      if (image.writePGM(outPicture->m_picture_frame, "output_first_frame.pgm") ==
          0) {
        g_first_frame_saved = true;
      } else {
        std::cerr << "Failed to write output_first_frame.pgm" << std::endl;
      }
    }
    if (g_output_file_type == OUTPUT_BMP) {
      string output_file;
      const uint32_t slice_type =
          outPicture->slice->slice_header->slice_type % 5;
      if (slice_type == SLICE_I)
        output_file = "output_I_" + to_string(index++) + ".bmp";
      else if (slice_type == SLICE_P)
        output_file = "output_P_" + to_string(index++) + ".bmp";
      else if (slice_type == SLICE_B)
        output_file = "output_B_" + to_string(index++) + ".bmp";
      else {
        std::cerr << "Unrecognized slice type:"
                  << outPicture->slice->slice_header->slice_type << std::endl;
        return -1;
      }
      image.saveToBmpFile(outPicture->m_picture_frame, output_file.c_str());
    } else if (g_output_file_type == OUTPUT_YUV)
      image.writeYUV(outPicture->m_picture_frame, "output.yuv");
  }
  return 0;
}
