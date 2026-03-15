#ifndef VVC_RECONSTRUCTION_PROBE_HPP_4M7P8K2R
#define VVC_RECONSTRUCTION_PROBE_HPP_4M7P8K2R

#include "VvcIntraCuProbe.hpp"
#include "VvcMiniParser.hpp"
#include "VvcResidualProbe.hpp"
#include "VvcTransformTreeProbe.hpp"
#include <string>
#include <vector>

struct VvcReconstructionProbeResult {
  bool valid = false;
  int bit_depth = 8;
  int qp = 0;
  int pred_dc = 0;
  int residual_min = 0;
  int residual_max = 0;
  int residual_sum = 0;
  int recon_min = 0;
  int recon_max = 0;
  int recon_sum = 0;
  int recon_top_left = 0;
  int recon_center = 0;
  int recon_bottom_right = 0;
  std::vector<int> residual;
  std::vector<int> recon;
};

class VvcReconstructionProbe {
 public:
  int reconstructFirstLumaTransformBlock(
      const VvcSpsState &sps, int slice_qp_y, bool dep_quant_used,
      const VvcIntraCuProbeResult &intra_result,
      const VvcTransformTreeProbeResult &transform_result,
      const VvcResidualProbeResult &residual_result,
      VvcReconstructionProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;

  void setError(const std::string &error) { m_last_error = error; }
};

#endif
