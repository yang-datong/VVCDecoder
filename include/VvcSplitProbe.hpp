#ifndef VVC_SPLIT_PROBE_HPP_4M6V6EKF
#define VVC_SPLIT_PROBE_HPP_4M6V6EKF

#include "VvcCabacReader.hpp"
#include "VvcMiniParser.hpp"
#include <string>

struct VvcSplitProbeResult {
  bool valid = false;
  int decisions = 0;
  int leaf_width = 0;
  int leaf_height = 0;
  std::string path;
};

class VvcSplitProbe {
 public:
  int probeFirstCuSplitPath(VvcCabacReader &cabac, const VvcSpsState &sps,
                            const VvcPictureHeaderState &picture,
                            int slice_type, int slice_qp_y,
                            VvcSplitProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;
  void setError(const std::string &error) { m_last_error = error; }
};

#endif
