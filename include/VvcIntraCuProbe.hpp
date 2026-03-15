#ifndef VVC_INTRA_CU_PROBE_HPP_9N5Q7X2K
#define VVC_INTRA_CU_PROBE_HPP_9N5Q7X2K

#include "VvcCabacReader.hpp"
#include "VvcMiniParser.hpp"
#include "VvcSplitProbe.hpp"
#include <array>
#include <string>

struct VvcIntraCuProbeResult {
  bool valid = false;
  int x0 = 0;
  int y0 = 0;
  int width = 0;
  int height = 0;

  bool mip_flag = false;
  bool mip_transposed_flag = false;
  int mip_mode = -1;

  int multi_ref_idx = 0;
  int isp_mode = 0;

  bool mpm_flag = false;
  int mpm_idx = -1;
  int rem_intra_luma_pred_mode = -1;
  int intra_luma_mode = -1;
  std::string intra_luma_source;
  std::array<int, 6> mpm_candidates = {};
};

class VvcIntraCuProbe {
 public:
  int probeFirstCuIntraSyntax(VvcCabacReader &cabac, const VvcSpsState &sps,
                              int slice_type, int slice_qp_y,
                              const VvcSplitProbeResult &split_result,
                              VvcIntraCuProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;

  void setError(const std::string &error) { m_last_error = error; }
};

#endif
