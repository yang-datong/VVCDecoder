#include "Slice.hpp"
#include "Frame.hpp"
#include "Nalu.hpp"
#include "SliceData.hpp"
#include "SliceHeader.hpp"
#include "Type.hpp"

Slice::Slice(Nalu *nalu) : m_nalu(nalu) {
  slice_header = new SliceHeader(m_nalu->nal_unit_type, m_nalu->nal_ref_idc);
  slice_data = new SliceData();
};

Slice::~Slice() {
  if (slice_header) {
    delete slice_header;
    slice_header = nullptr;
  }
  if (slice_data) {
    delete slice_data;
    slice_data = nullptr;
  }
}

int Slice::decode(BitStream &bs, Frame *(&dpb)[16], SPS &sps, PPS &pps,
                  Frame *frame) {
  //----------------帧----------------------------------
  frame->m_picture_coded_type = FRAME;
  frame->m_picture_frame.m_picture_coded_type = FRAME;
  frame->m_picture_frame.m_parent = frame;
  memcpy(frame->m_picture_frame.m_dpb, dpb, sizeof(Frame *) * MAX_DPB);
  frame->m_current_picture_ptr = &(frame->m_picture_frame);
  frame->m_picture_frame.init(this);

  slice_data->slice_segment_data(bs, frame->m_picture_frame, sps, pps);
  return 0;
}
