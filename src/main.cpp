#include "AnnexBReader.hpp"
#include "Frame.hpp"
#include "GOP.hpp"
#include "Image.hpp"
#include "Nalu.hpp"
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

struct VvcBitstreamSummary {
  int total_nalus = 0;
  int vcl_nalus = 0;
  int non_vcl_nalus = 0;
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
  int number = 0;

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
    cout << endl;

    if (result == 0) break;
  }

  cout << endl;
  cout << "VVC bitstream summary" << endl;
  cout << "  total_nalus: " << summary.total_nalus << endl;
  cout << "  vcl_nalus: " << summary.vcl_nalus << endl;
  cout << "  non_vcl_nalus: " << summary.non_vcl_nalus << endl;
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
