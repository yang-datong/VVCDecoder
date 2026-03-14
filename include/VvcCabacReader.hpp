#ifndef VVC_CABAC_READER_HPP_EC9V8L4K
#define VVC_CABAC_READER_HPP_EC9V8L4K

#include <cstdint>
#include <string>

class VvcCabacContextModel {
 public:
  VvcCabacContextModel();

  void init(int qp, uint8_t init_idc);
  void update(unsigned bin);
  void lpsMps(unsigned range, unsigned &lps, unsigned &mps) const;
  static uint8_t renormBitsLps(unsigned lps);

 private:
  static constexpr int kProbBits = 15;
  static constexpr int kProbBits0 = 10;
  static constexpr int kProbBits1 = 14;
  static constexpr uint16_t kMask0 =
      ((1u << kProbBits0) - 1) << (kProbBits - kProbBits0);
  static constexpr uint16_t kMask1 =
      ((1u << kProbBits1) - 1) << (kProbBits - kProbBits1);

  uint16_t m_state0 = 0;
  uint16_t m_state1 = 0;
  uint16_t m_rate0 = 0;
  uint16_t m_rate1 = 8;
  uint16_t m_delta0[2] = {0, 0};
  uint16_t m_delta1[2] = {0, 0};
};

class VvcCabacReader {
 public:
  int init(const uint8_t *buf, int len);
  int decodeBin(VvcCabacContextModel &ctx);

  int decodeBypass();
  int decodeTerminate();

  int bytesConsumed() const;
  int bytesRemaining() const;

  const std::string &lastError() const { return m_last_error; }

 private:
  std::string m_last_error;
  uint32_t m_range = 0;
  uint32_t m_value = 0;
  int32_t m_bits_needed = 0;
  const uint8_t *m_start = nullptr;
  const uint8_t *m_ptr = nullptr;
  const uint8_t *m_end = nullptr;

  int initFrom(const uint8_t *buf, const uint8_t *end);
  uint32_t readByte();
  void setError(const std::string &error) { m_last_error = error; }
};

#endif
