#ifndef TU_HPP_QOR1KUZZ
#define TU_HPP_QOR1KUZZ

#include <cstdint>
class TU {
 public:
  int cu_qp_delta;

  int res_scale_val;

  // Inferred parameters;
  int intra_pred_mode;
  int intra_pred_mode_c;
  int chroma_mode_c;
  uint8_t is_cu_qp_delta_coded;
  uint8_t is_cu_chroma_qp_offset_coded;
  int8_t cu_qp_offset_cb;
  int8_t cu_qp_offset_cr;
  uint8_t cross_pf;
};

#endif /* end of include guard: TU_HPP_QOR1KUZZ */
