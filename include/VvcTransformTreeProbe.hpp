#ifndef VVC_TRANSFORM_TREE_PROBE_HPP_F4R2M8QA
#define VVC_TRANSFORM_TREE_PROBE_HPP_F4R2M8QA

#include "VvcCabacReader.hpp"
#include "VvcIntraCuProbe.hpp"
#include "VvcMiniParser.hpp"
#include "VvcSplitProbe.hpp"
#include <string>

struct VvcTransformTreeProbeResult {
  bool valid = false;

  int first_tu_x0 = 0;
  int first_tu_y0 = 0;
  int first_tu_width = 0;
  int first_tu_height = 0;
  int first_tu_depth = 0;

  int first_tu_cbf_y = 0;
  int first_tu_cbf_cb = 0;
  int first_tu_cbf_cr = 0;
  bool first_tu_ts_flag_coded = false;
  int first_tu_ts_flag = 0;
  int first_tu_last_sig_x = -1;
  int first_tu_last_sig_y = -1;

  bool chroma_cclm_flag = false;
  bool chroma_dm_mode = false;
  int chroma_cand_idx = -1;
};

class VvcTransformTreeProbe {
 public:
  int probeFirstTransformUnit(VvcCabacReader &cabac, const VvcSpsState &sps,
                              int slice_type, int slice_qp_y,
                              const VvcSplitProbeResult &split_result,
                              const VvcIntraCuProbeResult &intra_result,
                              VvcTransformTreeProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;

  void setError(const std::string &error) { m_last_error = error; }
};

#endif
