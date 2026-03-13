#include "Nalu.hpp"
#include "BitStream.hpp"
#include "PictureBase.hpp"
#include "Slice.hpp"
#include <cstdint>

namespace {

int extractRbspPayload(const Nalu::EBSP &ebsp, Nalu::RBSP &rbsp,
                       uint8_t nalUnitHeaderBytes) {
  if (ebsp.buf == nullptr || ebsp.len <= nalUnitHeaderBytes) return -1;

  uint8_t *rbspBuffer = new uint8_t[ebsp.len - nalUnitHeaderBytes]{0};
  int32_t NumBytesInRBSP = 0;

  for (int i = nalUnitHeaderBytes; i < ebsp.len; i++) {
    if (i + 2 < ebsp.len && ebsp.buf[i] == 0x00 && ebsp.buf[i + 1] == 0x00 &&
        ebsp.buf[i + 2] == 0x03) {
      rbspBuffer[NumBytesInRBSP++] = 0x00;
      rbspBuffer[NumBytesInRBSP++] = 0x00;
      i += 2;
      continue;
    }
    rbspBuffer[NumBytesInRBSP++] = ebsp.buf[i];
  }

  rbsp.len = NumBytesInRBSP;
  rbsp.buf = rbspBuffer;
  return 0;
}

} // namespace

Nalu::~Nalu() {
  if (buffer != nullptr) {
    free(buffer);
    buffer = nullptr;
  }
}

Nalu::EBSP::~EBSP() {
  if (buf) delete[] buf;
}

Nalu::RBSP::~RBSP() {
  if (buf) delete[] buf;
}

int Nalu::setBuffer(uint8_t *buf, int len) {
  if (buffer != nullptr) {
    free(buffer);
    buffer = nullptr;
  }
  uint8_t *tmpBuf = (uint8_t *)malloc(len);
  memcpy(tmpBuf, buf, len);
  buffer = tmpBuf;
  this->len = len;
  return 0;
}

int Nalu::parseEBSP(EBSP &ebsp) {
  ebsp.len = len - startCodeLenth;
  uint8_t *ebspBuffer = new uint8_t[ebsp.len];
  memcpy(ebspBuffer, buffer + startCodeLenth, ebsp.len);
  ebsp.buf = ebspBuffer;
  return 0;
}

//7.3.1 NAL unit syntax
/* 注意，这里解析出来的RBSP是不包括RBSP head的一个字节的 */
int Nalu::parseRBSP(EBSP &ebsp, RBSP &rbsp) {
  uint8_t nalUnitHeaderBytes = 2;
  if (ebsp.buf == nullptr || ebsp.len <= nalUnitHeaderBytes) return -1;
  BitStream bs(ebsp.buf, ebsp.len);
  parseNALHeader(ebsp, &bs); // RBSP的头也是EBSP的头
  return extractRbspPayload(ebsp, rbsp, nalUnitHeaderBytes);
}

int Nalu::parseVvcRBSP(EBSP &ebsp, RBSP &rbsp) {
  uint8_t nalUnitHeaderBytes = 2;
  if (ebsp.buf == nullptr || ebsp.len <= nalUnitHeaderBytes) return -1;
  BitStream bs(ebsp.buf, ebsp.len);
  parseVvcNALHeader(ebsp, &bs);
  return extractRbspPayload(ebsp, rbsp, nalUnitHeaderBytes);
}

int Nalu::parseNALHeader(EBSP &ebsp) {
  uint8_t firstByte = ebsp.buf[0];
  nal_unit_type = firstByte & 0b00011111;
  /* 取低5bit，即0-4 bytes */
  nal_ref_idc = (firstByte & 0b01100000) >> 5;
  /* 取5-6 bytes */
  forbidden_zero_bit = firstByte >> 7;
  /* 取最高位，即7 byte */
  return 0;
}

int Nalu::parseNALHeader(EBSP &ebsp, BitStream *bs) {
  forbidden_zero_bit = bs->readU1();
  nal_unit_type = bs->readUn(6);
  nuh_layer_id = bs->readUn(6);
  nuh_temporal_id_plus1 = bs->readUn(3);
  TemporalId = nuh_temporal_id_plus1 - 1;
  return 0;
}

int Nalu::parseVvcNALHeader(EBSP &ebsp, BitStream *bs) {
  (void)ebsp;
  forbidden_zero_bit = bs->readU1();
  nuh_reserved_zero_bit = bs->readU1();
  nuh_layer_id = bs->readUn(6);
  nal_unit_type = bs->readUn(5);
  nuh_temporal_id_plus1 = bs->readUn(3);
  TemporalId = nuh_temporal_id_plus1 == 0 ? 0 : nuh_temporal_id_plus1 - 1;
  nal_ref_idc = 0;
  return 0;
}

bool Nalu::isVvcVclNaluType(uint8_t nal_unit_type) {
  return nal_unit_type <= VVC_NAL_UNIT_RESERVED_IRAP_VCL_11;
}

bool Nalu::isVvcSliceNaluType(uint8_t nal_unit_type) {
  switch (nal_unit_type) {
  case VVC_NAL_UNIT_CODED_SLICE_TRAIL:
  case VVC_NAL_UNIT_CODED_SLICE_STSA:
  case VVC_NAL_UNIT_CODED_SLICE_RADL:
  case VVC_NAL_UNIT_CODED_SLICE_RASL:
  case VVC_NAL_UNIT_CODED_SLICE_IDR_W_RADL:
  case VVC_NAL_UNIT_CODED_SLICE_IDR_N_LP:
  case VVC_NAL_UNIT_CODED_SLICE_CRA:
  case VVC_NAL_UNIT_CODED_SLICE_GDR:
    return true;
  default:
    return false;
  }
}

const char *Nalu::vvcNalUnitTypeToString(uint8_t nal_unit_type) {
  switch (nal_unit_type) {
  case VVC_NAL_UNIT_CODED_SLICE_TRAIL:
    return "TRAIL";
  case VVC_NAL_UNIT_CODED_SLICE_STSA:
    return "STSA";
  case VVC_NAL_UNIT_CODED_SLICE_RADL:
    return "RADL";
  case VVC_NAL_UNIT_CODED_SLICE_RASL:
    return "RASL";
  case VVC_NAL_UNIT_CODED_SLICE_IDR_W_RADL:
    return "IDR_W_RADL";
  case VVC_NAL_UNIT_CODED_SLICE_IDR_N_LP:
    return "IDR_N_LP";
  case VVC_NAL_UNIT_CODED_SLICE_CRA:
    return "CRA";
  case VVC_NAL_UNIT_CODED_SLICE_GDR:
    return "GDR";
  case VVC_NAL_UNIT_OPI:
    return "OPI";
  case VVC_NAL_UNIT_DCI:
    return "DCI";
  case VVC_NAL_UNIT_VPS:
    return "VPS";
  case VVC_NAL_UNIT_SPS:
    return "SPS";
  case VVC_NAL_UNIT_PPS:
    return "PPS";
  case VVC_NAL_UNIT_PREFIX_APS:
    return "Prefix APS";
  case VVC_NAL_UNIT_SUFFIX_APS:
    return "Suffix APS";
  case VVC_NAL_UNIT_PH:
    return "PH";
  case VVC_NAL_UNIT_ACCESS_UNIT_DELIMITER:
    return "AUD";
  case VVC_NAL_UNIT_EOS:
    return "EOS";
  case VVC_NAL_UNIT_EOB:
    return "EOB";
  case VVC_NAL_UNIT_PREFIX_SEI:
    return "Prefix SEI";
  case VVC_NAL_UNIT_SUFFIX_SEI:
    return "Suffix SEI";
  case VVC_NAL_UNIT_FD:
    return "FD";
  case VVC_NAL_UNIT_RESERVED_VCL_4:
  case VVC_NAL_UNIT_RESERVED_VCL_5:
  case VVC_NAL_UNIT_RESERVED_VCL_6:
  case VVC_NAL_UNIT_RESERVED_IRAP_VCL_11:
  case VVC_NAL_UNIT_RESERVED_NVCL_26:
  case VVC_NAL_UNIT_RESERVED_NVCL_27:
  case VVC_NAL_UNIT_UNSPECIFIED_28:
  case VVC_NAL_UNIT_UNSPECIFIED_29:
  case VVC_NAL_UNIT_UNSPECIFIED_30:
  case VVC_NAL_UNIT_UNSPECIFIED_31:
  default:
    return "UNK";
  }
}

int Nalu::extractVPSparameters(RBSP &rbsp, VPS vpss[MAX_SPS_COUNT],
                               uint32_t &curr_vps_id) {
  /* 初始化bit处理器，填充sps的数据 */
  BitStream bitStream(rbsp.buf, rbsp.len);
  VPS *vps = new VPS();
  vps->extractParameters(bitStream);
  vpss[vps->vps_video_parameter_set_id] = *vps;
  curr_vps_id = vps->vps_video_parameter_set_id;
  return 0;
}

/* 在T-REC-H.264-202108-I!!PDF-E.pdf -43页 */
int Nalu::extractSPSparameters(RBSP &rbsp, SPS spss[MAX_SPS_COUNT],
                               uint32_t &curr_sps_id, VPS vpss[MAX_SPS_COUNT]) {
  /* 初始化bit处理器，填充sps的数据 */
  BitStream bitStream(rbsp.buf, rbsp.len);
  SPS *sps = new SPS();
  sps->extractParameters(bitStream, vpss);
  spss[sps->seq_parameter_set_id] = *sps;
  curr_sps_id = sps->seq_parameter_set_id;
  return 0;
}

/* 在T-REC-H.264-202108-I!!PDF-E.pdf -47页 */
int Nalu::extractPPSparameters(RBSP &rbsp, PPS ppss[MAX_PPS_COUNT],
                               uint32_t &curr_pps_id,
                               uint32_t chroma_format_idc,
                               SPS spss[MAX_SPS_COUNT]) {
  BitStream bitStream(rbsp.buf, rbsp.len);
  PPS *pps = new PPS();
  pps->extractParameters(bitStream, chroma_format_idc, spss);
  ppss[pps->pic_parameter_set_id] = *pps;
  curr_pps_id = pps->pic_parameter_set_id;
  return 0;
}

/* 在T-REC-H.264-202108-I!!PDF-E.pdf -48页 */
int Nalu::extractSEIparameters(RBSP &rbsp, SEI &sei, SPS &sps) {
  sei._buf = rbsp.buf;
  sei._len = rbsp.len;
  sei.extractParameters(sps);
  return 0;
}

int Nalu::extractSliceparameters(BitStream &bitStream, GOP &gop, Frame &frame) {
  Slice *slice = new Slice(this);
  slice->slice_header->slice_segment_header(bitStream, gop);
  frame.slice = slice;
  return 0;
}

int Nalu::extractIDRparameters(BitStream &bitStream, GOP &gop, Frame &frame) {
  extractSliceparameters(bitStream, gop, frame);
  return 0;
}
