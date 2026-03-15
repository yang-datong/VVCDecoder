#include "VvcCabacReader.hpp"
#include <array>
#include <cstdint>

namespace {

constexpr std::array<uint8_t, 32> kRenormTable32 = {
    6, 5, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

} // namespace

VvcCabacContextModel::VvcCabacContextModel() {
  const uint16_t half = 1u << 14;
  m_state0 = half;
  m_state1 = half;
  m_rate0 = 0;
  m_rate1 = 8;
  m_delta0[0] = static_cast<uint16_t>(0xFFFFu >> (16 - m_rate0));
  m_delta0[1] = static_cast<uint16_t>(0xFFFFu >> (16 - 15));
  m_delta1[0] = static_cast<uint16_t>(0xFFFFu >> (16 - m_rate1));
  m_delta1[1] = static_cast<uint16_t>(0xFFFFu >> (16 - 15));
}

void VvcCabacContextModel::init(int qp, uint8_t init_idc) {
  const int slope = (init_idc >> 3) - 4;
  const int offset = ((init_idc & 7) * 18) + 1;
  const int init_state = ((slope * (qp - 16)) >> 1) + offset;
  const int clipped_state = init_state < 1 ? 1 : (init_state > 127 ? 127 : init_state);
  const int p1 = clipped_state << 8;

  m_state0 = static_cast<uint16_t>(p1 & kMask0);
  m_state1 = static_cast<uint16_t>(p1 & kMask1);
}

void VvcCabacContextModel::update(unsigned bin) {
  int delta0 = static_cast<int>(m_delta0[bin]) - static_cast<int>(m_state0);
  int delta1 = static_cast<int>(m_delta1[bin]) - static_cast<int>(m_state1);
  delta0 >>= m_rate0;
  delta1 >>= m_rate1;
  m_state0 = static_cast<uint16_t>(m_state0 + (delta0 << 5));
  m_state1 = static_cast<uint16_t>(m_state1 + (delta1 << 1));
}

void VvcCabacContextModel::lpsMps(unsigned range, unsigned &lps, unsigned &mps) const {
  const uint16_t q = static_cast<uint16_t>((m_state0 + m_state1) >> 8);
  mps = q >> 7;

  const uint16_t inv = static_cast<uint16_t>((-static_cast<int>(mps)) & 0xFF);
  lps = (((((q ^ inv) >> 2) * (range >> 5)) >> 1) + 4);
}

uint8_t VvcCabacContextModel::renormBitsLps(unsigned lps) {
  return kRenormTable32[(lps >> 3) & 31];
}

int VvcCabacReader::init(const uint8_t *buf, int len) {
  m_last_error.clear();
  if (buf == nullptr || len <= 1) {
    setError("Invalid VVC CABAC payload span");
    return -1;
  }
  return initFrom(buf, buf + len);
}

int VvcCabacReader::initFrom(const uint8_t *buf, const uint8_t *end) {
  if (buf == nullptr || end == nullptr || end <= buf + 1) {
    setError("Invalid VVC CABAC payload span");
    return -1;
  }

  m_start = buf;
  m_ptr = buf;
  m_end = end;
  m_range = 510;
  m_value = (readByte() << 8) + readByte();
  m_bits_needed = -8;

  if (m_range < 256) {
    setError("Failed to initialize VVC CABAC arithmetic engine");
    return -1;
  }
  return 0;
}

uint32_t VvcCabacReader::readByte() {
  if (m_ptr == nullptr || m_ptr >= m_end) return 0;
  return *m_ptr++;
}

uint32_t VvcCabacReader::readByteFlag(bool flag) {
  if (!flag) return 0;
  return readByte();
}

int VvcCabacReader::decodeBin(VvcCabacContextModel &ctx) {
  unsigned bin = 0;
  unsigned lps = 0;
  uint32_t range = m_range;
  uint32_t value = m_value;
  int32_t bits_needed = m_bits_needed;

  ctx.lpsMps(range, lps, bin);

  range -= lps;
  const uint32_t scaled_range = range << 7;

  const int b = ~((static_cast<int>(value) - static_cast<int>(scaled_range)) >> 31);
  const int a = ~b & ((static_cast<int>(range) - 256) >> 31);
  const int num_bits =
      (a & 1) | (b & VvcCabacContextModel::renormBitsLps(lps));

  value -= static_cast<uint32_t>(b & static_cast<int>(scaled_range));
  value <<= num_bits;

  range &= ~static_cast<uint32_t>(b);
  range |= static_cast<uint32_t>(b & static_cast<int>(lps));
  range <<= num_bits;

  bin ^= static_cast<unsigned>(b);
  bin &= 1u;

  bits_needed += num_bits & (a | b);
  const int c = ~(bits_needed >> 31);
  value += readByteFlag((c & 1) != 0) << (bits_needed & 31);
  bits_needed -= c & 8;

  m_range = range;
  m_value = value;
  m_bits_needed = bits_needed;
  ctx.update(bin);
  return static_cast<int>(bin);
}

int VvcCabacReader::decodeBypass() {
  m_value <<= 1;
  if (++m_bits_needed >= 0) {
    m_value += readByte();
    m_bits_needed = -8;
  }

  const uint32_t scaled_range = m_range << 7;
  if (m_value < scaled_range) {
    return 0;
  }

  m_value -= scaled_range;
  return 1;
}

uint32_t VvcCabacReader::decodeBinsEP(int num_bins) {
  if (num_bins <= 0) return 0;
  uint32_t bins = 0;
  for (int i = 0; i < num_bins; ++i) {
    bins = (bins << 1) | static_cast<uint32_t>(decodeBypass() & 1);
  }
  return bins;
}

uint32_t VvcCabacReader::decodeRemAbsEP(unsigned go_rice_par, unsigned cutoff,
                                        int max_log2_tr_dynamic_range) {
  unsigned prefix = 0;
  {
    const unsigned max_prefix =
        32u - static_cast<unsigned>(max_log2_tr_dynamic_range);
    unsigned code_word = 0;
    do {
      prefix++;
      code_word = static_cast<unsigned>(decodeBypass() & 1);
    } while (code_word && prefix < max_prefix);
    prefix -= 1u - code_word;
  }

  unsigned length = go_rice_par;
  unsigned offset = 0;
  if (prefix < cutoff) {
    offset = prefix << go_rice_par;
  } else {
    offset = (((1u << (prefix - cutoff)) + cutoff - 1u) << go_rice_par);
    length +=
        (prefix == (32u - static_cast<unsigned>(max_log2_tr_dynamic_range)))
            ? static_cast<unsigned>(max_log2_tr_dynamic_range) - go_rice_par
            : prefix - cutoff;
  }

  return offset + decodeBinsEP(static_cast<int>(length));
}

int VvcCabacReader::decodeTerminate() {
  m_range -= 2;
  if (m_value < (m_range << 7)) {
    if (m_range < 256) {
      m_range <<= 1;
      m_value <<= 1;
      if (++m_bits_needed == 0) {
        m_value += readByte();
        m_bits_needed = -8;
      }
    }
    return 0;
  }
  return 1;
}

int VvcCabacReader::bytesConsumed() const {
  if (m_start == nullptr || m_ptr == nullptr) return 0;
  return static_cast<int>(m_ptr - m_start);
}

int VvcCabacReader::bytesRemaining() const {
  if (m_ptr == nullptr || m_end == nullptr || m_end < m_ptr) return 0;
  return static_cast<int>(m_end - m_ptr);
}
