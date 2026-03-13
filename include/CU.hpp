#ifndef CU_HPP_583NY9QE
#define CU_HPP_583NY9QE

#include "CommonDef.h"
#include <cstdint>
class CU {
 public:
  int x;
  int y;

  PredMode CuPredMode; ///< PredMode
  PartMode part_mode;  ///< PartMode

  // Inferred parameters
  uint8_t IntraSplitFlag;  ///< IntraSplitFlag
  uint8_t max_trafo_depth; ///< MaxTrafoDepth
  uint8_t cu_transquant_bypass_flag;
};

#endif /* end of include guard: CU_HPP_583NY9QE */
