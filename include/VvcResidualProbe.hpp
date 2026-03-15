#ifndef VVC_RESIDUAL_PROBE_HPP_H6K4Q2VR
#define VVC_RESIDUAL_PROBE_HPP_H6K4Q2VR

#include "VvcCabacReader.hpp"
#include "VvcMiniParser.hpp"
#include "VvcTransformTreeProbe.hpp"
#include <string>
#include <vector>

struct VvcResidualProbeResult {
  bool valid = false;
  int scan_pos_last = -1;
  int last_subset_id = -1;
  int sig_group_count = 0;
  int first_sig_subset_id = -1;
  int non_zero_coeffs = 0;
  int abs_sum = 0;
  int max_abs_level = 0;
  int last_coeff_value = 0;
  std::vector<int> coeffs;
};

class VvcResidualProbe {
 public:
  int probeFirstTuResidualSyntax(VvcCabacReader &cabac, const VvcSpsState &sps,
                                 int slice_type, int slice_qp_y,
                                 bool dep_quant_used,
                                 bool sign_data_hiding_used,
                                 const VvcTransformTreeProbeResult &tu_result,
                                 VvcResidualProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;

  void setError(const std::string &error) { m_last_error = error; }
};

#endif
