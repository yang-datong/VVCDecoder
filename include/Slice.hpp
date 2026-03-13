#ifndef SLICE_HPP_BRS58Q9D
#define SLICE_HPP_BRS58Q9D

#include "SliceData.hpp"
#include "SliceHeader.hpp"

class Nalu;
class PictureBase;
class Frame;

class Slice {
 private:
  Nalu *m_nalu;

 public:
  /* TODO: 同时也需要当前使用的SPS、PPS，因为header、Data内的SPS,PPS是不允许对外提供的，相当于Slice是一个对外类，header、data是Slice的内部类，只能由Slice操作 */
  explicit Slice(Nalu *nalu);
  ~Slice();

  int decode(BitStream &bitStream, Frame *(&dpb)[16], SPS &sps, PPS &pps,
             Frame *frame);
  Nalu *getNalu() const { return m_nalu; }

 public:
  SliceHeader *slice_header;
  SliceData *slice_data;
};

#endif /* end of include guard: SLICE_CPP_BRS58Q9D */
