#ifndef PU_HPP_PG5G3NE4
#define PU_HPP_PG5G3NE4

#include <cstdint>
class PU {
 public:
  int mpm_idx;
  int rem_intra_luma_pred_mode;
  uint8_t intra_pred_mode[4];
  //Mv mvd;
  uint8_t merge_flag;
  uint8_t intra_pred_mode_c[4];
  uint8_t intra_chroma_pred_mode[4];
};

#endif /* end of include guard: PU_HPP_PG5G3NE4 */
