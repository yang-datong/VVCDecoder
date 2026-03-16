#ifndef VVC_CTU_PREFIX_PROBE_HPP_65H8TX7K
#define VVC_CTU_PREFIX_PROBE_HPP_65H8TX7K

#include "VvcCabacReader.hpp"
#include "VvcMiniParser.hpp"
#include <string>

struct VvcCtuPrefixProbeResult {
  bool valid = false;
  int bins_consumed = 0;
  int sao_bins = 0;
  int alf_bins = 0;
  bool sao_consumed = false;
  bool alf_consumed = false;
  bool alf_luma_enabled = false;
  bool alf_use_temporal = false;
  std::string sao_luma_mode;
  std::string sao_chroma_mode;
  int sao_luma_band_position = -1;
  int sao_luma_offsets[4] = {0, 0, 0, 0};
};

class VvcCtuPrefixProbe {
 public:
  int consumeFirstCtuPrefix(VvcCabacReader &cabac, const VvcSpsState &sps,
                            int slice_type, int slice_qp_y,
                            const VvcFrameHeaderSummary &summary,
                            VvcCtuPrefixProbeResult &result);

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;
  void setError(const std::string &error) { m_last_error = error; }
};

#endif
