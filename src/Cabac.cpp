#include "Cabac.hpp"
#include "Constants.hpp"
#include "MacroBlock.hpp"
#include "PictureBase.hpp"
#include "SliceHeader.hpp"
#include "Type.hpp"
#include <cstdint>
#include <cstring>

#define H264_MLPS_STATE_OFFSET 1024
static const uint8_t *const ff_h264_mlps_state =
    ff_h264_cabac_tables + H264_MLPS_STATE_OFFSET;

#define H264_NORM_SHIFT_OFFSET 0
static const uint8_t *const ff_h264_norm_shift =
    ff_h264_cabac_tables + H264_NORM_SHIFT_OFFSET;

#define CABAC_BITS 16
#define CABAC_MASK ((1 << CABAC_BITS) - 1)

static int ff_init_cabac_decoder_like_ffmpeg(const uint8_t *&bytestream_start,
                                             const uint8_t *&bytestream,
                                             const uint8_t *&bytestream_end,
                                             int32_t &ivlOffset,
                                             int32_t &ivlCurrRange,
                                             const uint8_t *buf,
                                             const uint8_t *end) {
  if (buf == nullptr || end == nullptr || end <= buf + 1) return -1;

  bytestream_start = bytestream = buf;
  bytestream_end = end;

  ivlOffset = (*bytestream++) << 18;
  ivlOffset += (*bytestream++) << 10;
  // 与 ffmpeg 保持一致：根据当前 bytestream 对齐状态选择初始 low 注入路径。
  if (((uintptr_t)bytestream & 1) == 0) {
    ivlOffset += (1 << 9);
  } else {
    if (bytestream >= bytestream_end) return -1;
    ivlOffset += ((*bytestream++) << 2) + 2;
  }

  ivlCurrRange = 0x1FE;
  if ((ivlCurrRange << (CABAC_BITS + 1)) < ivlOffset) return -1;
  return 0;
}

/* 对于P、SP和B Slice 类型，初始化还取决于cabac_init_idc语法元素的值 */
int Cabac::init_m_n(int32_t ctxIdx, H264_SLICE_TYPE slice_type,
                    int32_t cabac_init_idc, int32_t &m, int32_t &n) {
  RET(ctxIdx < 0 || ctxIdx > 1024 || cabac_init_idc < 0 || cabac_init_idc > 2);

  if (ctxIdx >= 0 && ctxIdx <= 10) {
    // Table 9-12 – Values of variables m and n for ctxIdx from 0 to 10
    m = mn_0_10[ctxIdx][0];
    n = mn_0_10[ctxIdx][1];
  } else if (ctxIdx >= 11 && ctxIdx <= 23) {
    // Table 9-13 – Values of variables m and n for ctxIdx from 11 to 23
    m = mn_11_23[cabac_init_idc][ctxIdx - 11][0];
    n = mn_11_23[cabac_init_idc][ctxIdx - 11][1];
  } else if (ctxIdx >= 24 && ctxIdx <= 39) {
    // Table 9-14 – Values of variables m and n for ctxIdx from 24 to 39
    m = mn_24_39[cabac_init_idc][ctxIdx - 24][0];
    n = mn_24_39[cabac_init_idc][ctxIdx - 24][1];
  } else if (ctxIdx >= 40 && ctxIdx <= 53) {
    // Table 9-15 – Values of variables m and n for ctxIdx from 40 to 53
    m = mn_40_53[cabac_init_idc][ctxIdx - 40][0];
    n = mn_40_53[cabac_init_idc][ctxIdx - 40][1];
  } else if (ctxIdx >= 54 && ctxIdx <= 59) {
    // Table 9-16 – Values of variables m and n for ctxIdx from 54 to 59, and 399 to 401
    m = mn_54_59[cabac_init_idc][ctxIdx - 54][0];
    n = mn_54_59[cabac_init_idc][ctxIdx - 54][1];
  } else if (ctxIdx >= 60 && ctxIdx <= 69) {
    // Table 9-17 – Values of variables m and n for ctxIdx from 60 to 69
    m = mn_60_69[ctxIdx - 60][0];
    n = mn_60_69[ctxIdx - 60][1];
  } else if (ctxIdx >= 70 && ctxIdx <= 104) {
    // Table 9-18 – Values of variables m and n for ctxIdx from 70 to 104
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_70_104[0][ctxIdx - 70][0];
      n = mn_70_104[0][ctxIdx - 70][1];
    } else {
      m = mn_70_104[cabac_init_idc + 1][ctxIdx - 70][0];
      n = mn_70_104[cabac_init_idc + 1][ctxIdx - 70][1];
    }
  } else if (ctxIdx >= 105 && ctxIdx <= 165) {
    // Table 9-19 – Values of variables m and n for ctxIdx from 105 to 165
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_105_165[0][ctxIdx - 105][0];
      n = mn_105_165[0][ctxIdx - 105][1];
    } else {
      m = mn_105_165[cabac_init_idc + 1][ctxIdx - 105][0];
      n = mn_105_165[cabac_init_idc + 1][ctxIdx - 105][1];
    }
  } else if (ctxIdx >= 166 && ctxIdx <= 226) {
    // Table 9-20 – Values of variables m and n for ctxIdx from 166 to 226
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_166_226[0][ctxIdx - 166][0];
      n = mn_166_226[0][ctxIdx - 166][1];
    } else {
      m = mn_166_226[cabac_init_idc + 1][ctxIdx - 166][0];
      n = mn_166_226[cabac_init_idc + 1][ctxIdx - 166][1];
    }
  } else if (ctxIdx >= 227 && ctxIdx <= 275) {
    // Table 9-21 – Values of variables m and n for ctxIdx from 227 to 275
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_227_275[0][ctxIdx - 227][0];
      n = mn_227_275[0][ctxIdx - 227][1];
    } else {
      m = mn_227_275[cabac_init_idc + 1][ctxIdx - 227][0];
      n = mn_227_275[cabac_init_idc + 1][ctxIdx - 227][1];
    }
  } else if (ctxIdx == 276) {
    /* NOTE: ctxIdx = 276与end_of_slice_flag和mb_type的bin相关联，mb_type指定I_PCM macroblock类型。9.3.3.2.4中规定的解码过程适用于等于276的ctxIdx。这个解码过程，然而，也可以通过使用第9.3.3.2.1小节中指定的解码过程来实现。在本例中，与ctxIdx = 276相关联的初始值被指定为pStateIdx = 63和valMPS = 0，其中pStateIdx = 63表示非自适应概率状态。 */
    return -1;
  } else if (ctxIdx >= 277 && ctxIdx <= 337) {
    // Table 9-22 – Values of variables m and n for ctxIdx from 277 to 337
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_277_337[0][ctxIdx - 277][0];
      n = mn_277_337[0][ctxIdx - 277][1];
    } else {
      m = mn_277_337[cabac_init_idc + 1][ctxIdx - 277][0];
      n = mn_277_337[cabac_init_idc + 1][ctxIdx - 277][1];
    }
  } else if (ctxIdx >= 338 && ctxIdx <= 398) {
    // Table 9-23 – Values of variables m and n for ctxIdx from 338 to 398
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_338_398[0][ctxIdx - 338][0];
      n = mn_338_398[0][ctxIdx - 338][1];
    } else {
      m = mn_338_398[cabac_init_idc + 1][ctxIdx - 338][0];
      n = mn_338_398[cabac_init_idc + 1][ctxIdx - 338][1];
    }
  } else if (ctxIdx >= 399 && ctxIdx <= 401) {
    if (slice_type == SLICE_I) {
      m = mn_399_401[0][ctxIdx - 399][0];
      n = mn_399_401[0][ctxIdx - 399][1];
    } else {
      m = mn_399_401[cabac_init_idc + 1][ctxIdx - 399][0];
      n = mn_399_401[cabac_init_idc + 1][ctxIdx - 399][1];
    }
  } else if (ctxIdx >= 402 && ctxIdx <= 459) {
    // Table 9-24 – Values of variables m and n for ctxIdx from 402 to 459
    if (slice_type == SLICE_I) {
      m = mn_402_459[0][ctxIdx - 402][0];
      n = mn_402_459[0][ctxIdx - 402][1];
    } else {
      m = mn_402_459[cabac_init_idc + 1][ctxIdx - 402][0];
      n = mn_402_459[cabac_init_idc + 1][ctxIdx - 402][1];
    }
  } else if (ctxIdx >= 460 && ctxIdx <= 483) {
    // Table 9-25 – Values of variables m and n for ctxIdx from 460 to 483
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_460_483[0][ctxIdx - 460][0];
      n = mn_460_483[0][ctxIdx - 460][1];
    } else {
      m = mn_460_483[cabac_init_idc + 1][ctxIdx - 460][0];
      n = mn_460_483[cabac_init_idc + 1][ctxIdx - 460][1];
    }
  } else if (ctxIdx >= 484 && ctxIdx <= 571) {
    // Table 9-26 – Values of variables m and n for ctxIdx from 484 to 571
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_484_571[0][ctxIdx - 484][0];
      n = mn_484_571[0][ctxIdx - 484][1];
    } else {
      m = mn_484_571[cabac_init_idc + 1][ctxIdx - 484][0];
      n = mn_484_571[cabac_init_idc + 1][ctxIdx - 484][1];
    }
  } else if (ctxIdx >= 572 && ctxIdx <= 659) {
    // Table 9-27 – Values of variables m and n for ctxIdx from 572 to 659
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_572_659[0][ctxIdx - 572][0];
      n = mn_572_659[0][ctxIdx - 572][1];
    } else {
      m = mn_572_659[cabac_init_idc + 1][ctxIdx - 572][0];
      n = mn_572_659[cabac_init_idc + 1][ctxIdx - 572][1];
    }
  } else if (ctxIdx >= 660 && ctxIdx <= 717) {
    // Table 9-28 – Values of variables m and n for ctxIdx from 660 to 717
    if (slice_type == SLICE_I) {
      m = mn_660_717[0][ctxIdx - 660][0];
      n = mn_660_717[0][ctxIdx - 660][1];
    } else {
      m = mn_660_717[cabac_init_idc + 1][ctxIdx - 660][0];
      n = mn_660_717[cabac_init_idc + 1][ctxIdx - 660][1];
    }
  } else if (ctxIdx >= 718 && ctxIdx <= 775) {
    // Table 9-29 – Values of variables m and n for ctxIdx from 718 to 775
    if (slice_type == SLICE_I) {
      m = mn_718_775[0][ctxIdx - 718][0];
      n = mn_718_775[0][ctxIdx - 718][1];
    } else {
      m = mn_718_775[cabac_init_idc + 1][ctxIdx - 718][0];
      n = mn_718_775[cabac_init_idc + 1][ctxIdx - 718][1];
    }
  } else if (ctxIdx >= 776 && ctxIdx <= 863) {
    // Table 9-30 – Values of variables m and n for ctxIdx from 776 to 863
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_776_863[0][ctxIdx - 776][0];
      n = mn_776_863[0][ctxIdx - 776][1];
    } else {
      m = mn_776_863[cabac_init_idc + 1][ctxIdx - 776][0];
      n = mn_776_863[cabac_init_idc + 1][ctxIdx - 776][1];
    }
  } else if (ctxIdx >= 864 && ctxIdx <= 951) {
    // Table 9-31 – Values of variables m and n for ctxIdx from 864 to 951
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_864_951[0][ctxIdx - 864][0];
      n = mn_864_951[0][ctxIdx - 864][1];
    } else {
      m = mn_864_951[cabac_init_idc + 1][ctxIdx - 864][0];
      n = mn_864_951[cabac_init_idc + 1][ctxIdx - 864][1];
    }
  } else if (ctxIdx >= 952 && ctxIdx <= 1011) {
    // Table 9-32 – Values of variables m and n for ctxIdx from 952 to 1011
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_952_1011[0][ctxIdx - 952][0];
      n = mn_952_1011[0][ctxIdx - 952][1];
    } else {
      m = mn_952_1011[cabac_init_idc + 1][ctxIdx - 952][0];
      n = mn_952_1011[cabac_init_idc + 1][ctxIdx - 952][1];
    }
  } else if (ctxIdx >= 1012 && ctxIdx <= 1023) {
    // Table 9-33 – Values of variables m and n for ctxIdx from 1012 to 1023
    if (slice_type == SLICE_I || slice_type == SLICE_SI) {
      m = mn_1012_1023[0][ctxIdx - 1012][0];
      n = mn_1012_1023[0][ctxIdx - 1012][1];
    } else {
      m = mn_1012_1023[cabac_init_idc + 1][ctxIdx - 1012][0];
      n = mn_1012_1023[cabac_init_idc + 1][ctxIdx - 1012][1];
    }
  } else
    RET(-1);
  return 0;
}

// 9.3.1.1 Initialisation process for context variables
int Cabac::init_of_context_variables(H264_SLICE_TYPE slice_type,
                                     int32_t cabac_init_idc, int32_t SliceQPY) {

  return 0;
}

// 9.3.1.2 Initialisation process for the arithmetic decoding engine
int Cabac::init_of_decoding_engine() {
  // 编码间隔：较大的值代表了最大的概率区间, 编码位移
  ivlCurrRange = 510, ivlOffset = bs.readUn(9);
  RET(ivlOffset == 510 || ivlOffset == 511);
  return 0;
}

//NOTE: 首次调用的CABAC解码
/* 9.3.3.1.1.1 Derivation process of ctxIdxInc for the syntax element mb_skip_flag */
int Cabac::decode_mb_skip_flag(int32_t currMbAddr, int32_t &synElVal) {
  const int slice_type = picture.m_slice->slice_header->slice_type % 5;
  // Table 9-34 : slice_type == SLICE_P,SLICE_SP, SLICE_B)
  int32_t ctxIdxOffset = (slice_type == SLICE_B) ? 24 : 11;

  // 获取ctxIdxInc后才能进行算术解码
  int32_t ctxIdxInc;
  RET(derivation_ctxIdxInc_for_mb_skip_flag(currMbAddr, ctxIdxInc));

  //NOTE:如果没有为表 9-34 中标记的相应二值化或二值化部分的 ctxIdxOffset 分配值作为“na”，相应二值化或二值化前缀/后缀部分的比特串的所有bin通过调用第9.3.3.2.3节中指定的DecodeBypass过程来解码。在这种情况下，bypassFlag被设置为等于1，其中bypassFlag用于指示为了从比特流解析bin的值，应用DecodeBypass过程。

  int bypassFlag = (ctxIdxOffset == NA);
  int ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, synElVal));
  return 0;
}

/* 9.3.3.1.1.1 Derivation process of ctxIdxInc for the syntax element mb_skip_flag */
/* 输出: ctxIdxInc */
int Cabac::derivation_ctxIdxInc_for_mb_skip_flag(int32_t currMbAddr,
                                                 int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;
  int32_t mbAddrA = 0, mbAddrB = 0;
  RET(picture.derivation_for_neighbouring_macroblocks(
      MbaffFrameFlag, currMbAddr, mbAddrA, mbAddrB, 0));

  int32_t condTermFlagA =
      (mbAddrA < 0 || picture.m_mbs[mbAddrA].mb_skip_flag) ? 0 : 1;
  int32_t condTermFlagB =
      (mbAddrB < 0 || picture.m_mbs[mbAddrB].mb_skip_flag) ? 0 : 1;

  // ctxIdxInc = [0,2]
  ctxIdxInc = condTermFlagA + condTermFlagB;

  return 0;
}

// 9.3.3.1.1.2 Derivation process of ctxIdxInc for the syntax element mb_field_decoding_flag
int Cabac::decode_mb_field_decoding_flag(int32_t &synElVal) {
  int ctxIdxInc = 0;
  RET(derivation_ctxIdxInc_for_mb_field_decoding_flag(ctxIdxInc));

  int ctxIdxOffset = 70;
  int bypassFlag = (ctxIdxOffset == -1) ? 1 : 0;
  int ctxIdx = ctxIdxOffset + ctxIdxInc;

  int &bin = synElVal;
  RET(decodeBin(bypassFlag, ctxIdx, bin)); // binIdx = 0;
  return 0;
}

// 9.3.3.1.1.2 Derivation process of ctxIdxInc for the syntax element mb_field_decoding_flag
int Cabac::derivation_ctxIdxInc_for_mb_field_decoding_flag(int32_t &ctxIdxInc) {

  /* Table 6-4 – Specification of mbAddrN and yM */
  int32_t mbAddrA, mbAddrB, mbAddrC, mbAddrD;
  /* 调用第 6.4.10 节中指定的相邻宏块地址及其在 MBAFF 帧中的可用性的导出过程，并将输出分配给 mbAddrA 和 mbAddrB。  */
  picture.derivation_for_neighbouring_macroblock_addr_availability_in_MBAFF(
      mbAddrA, mbAddrB, mbAddrC, mbAddrD);

  /* 当宏块mbAddrN和mbAddrN+1两者都具有等于P_Skip或B_Skip的mb_type时，将如第7.4.4节中指定的语法元素mb_field_decoding_flag的推断规则应用于宏块mbAddrN。  
   * 让变量 condTermFlagN（N 为 A 或 B）按如下方式导出： 
   * — 如果以下任一条件为真，则 condTermFlagN 设置为等于 0： 
      * — mbAddrN 不可用， 
      * — 宏块 mbAddrN 是帧宏块。  
   * – 否则，condTermFlagN 设置为等于 1。*/

  int32_t condTermFlagA = 1, condTermFlagB = 1;
  if (mbAddrA < 0 || picture.m_mbs[mbAddrA].mb_field_decoding_flag == 0)
    condTermFlagA = 0;

  if (mbAddrB < 0 || picture.m_mbs[mbAddrB].mb_field_decoding_flag == 0)
    condTermFlagB = 0;

  // 变量 ctxIdxInc 由以下公式得出： ctxIdxInc = condTermFlagA + condTermFlagB
  ctxIdxInc = condTermFlagA + condTermFlagB;

  return 0;
}

// 9.3.3.1.1.3 Derivation process of ctxIdxInc for the syntax element mb_type
int Cabac::decode_mb_type(int32_t &synElVal) {
  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int ret = 0;
  if ((slice_type % 5) == SLICE_SI)
    ret = decode_mb_type_in_SI_slices(synElVal);
  else if ((slice_type % 5) == SLICE_I)
    ret = decode_mb_type_in_I_slices(3, synElVal);
  else if ((slice_type % 5) == SLICE_P || (slice_type % 5) == SLICE_SP)
    ret = decode_mb_type_in_P_SP_slices(synElVal);
  else if ((slice_type % 5) == SLICE_B)
    ret = decode_mb_type_in_B_slices(synElVal);
  else
    RET(-1);
  RET(ret);
  return 0;
}

int Cabac::decode_mb_type_in_SI_slices(int32_t &synElVal) {
  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  RET((slice_type % 5) != SLICE_SI);

  int32_t ctxIdxOffset = 0, ctxIdxInc = 0;
  RET(derivation_ctxIdxInc_for_mb_type(ctxIdxOffset, ctxIdxInc));
  int32_t binVal = 0;
  RET(decodeBin((ctxIdxOffset == NA), ctxIdxOffset + ctxIdxInc, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    RET(decode_mb_type_in_I_slices(3, synElVal));
    synElVal += 1;
  }

  return 0;
}

int Cabac::decode_mb_type_in_I_slices(int32_t ctxIdxOffset, int32_t &synElVal) {
  int32_t ctxIdxInc = 0;
  if (ctxIdxOffset == 3)
    RET(derivation_ctxIdxInc_for_mb_type(ctxIdxOffset, ctxIdxInc));

  int32_t binVal = 0;
  int32_t ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(false, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = 276;
    RET(decodeBin(false, ctxIdx, binVal));
    if (binVal == 0) {
      ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 3 : 1);
      RET(decodeBin(false, ctxIdx, binVal));
      if (binVal == 0) {
        ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 4 : 2);
        RET(decodeBin(false, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = (ctxIdxOffset == 3) ? ctxIdxOffset + 6 : ctxIdxOffset + 3;
          RET(decodeBin(false, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 1 : 2;
          } else if (binVal == 1) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 3 : 4;
          }
        } else if (binVal == 1) {
          ctxIdx = (ctxIdxOffset == 3) ? ctxIdxOffset + 5 : ctxIdxOffset + 2;
          RET(decodeBin(false, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 6 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 5 : 6;
            } else if (binVal == 1) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 7 : 8;
            }
          } else if (binVal == 1) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 6 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 9 : 10;
            } else if (binVal == 1) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 11 : 12;
            }
          }
        }
      } else if (binVal == 1) {
        ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 4 : 2);
        RET(decodeBin(false, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = (ctxIdxOffset == 3) ? ctxIdxOffset + 6 : ctxIdxOffset + 3;
          RET(decodeBin(false, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 13 : 14;
          } else if (binVal == 1) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 15 : 16;
          }
        } else if (binVal == 1) {
          ctxIdx = (ctxIdxOffset == 3) ? ctxIdxOffset + 5 : ctxIdxOffset + 2;
          RET(decodeBin(false, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 6 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 17 : 18;
            } else if (binVal == 1) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 19 : 20;
            }
          } else if (binVal == 1) {
            ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 6 : 3);
            RET(decodeBin(false, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 21 : 22;
            } else if (binVal == 1) {
              ctxIdx = ctxIdxOffset + ((ctxIdxOffset == 3) ? 7 : 3);
              RET(decodeBin(false, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 23 : 24;
            }
          }
        }
      }
    } else
      synElVal = 25;
  }

  if (synElVal == 25) init_of_decoding_engine();
  return 0;
}

int Cabac::decode_mb_type_in_P_SP_slices(int32_t &synElVal) {

  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  RET((slice_type % 5) != SLICE_P && (slice_type % 5) != SLICE_SP);
  ctxIdxOffset = 14;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  if (binVal == 0) {
    ctxIdx = ctxIdxOffset + 1;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));
    if (binVal == 0) {
      ctxIdx = ctxIdxOffset + 2;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 0) ? 0 : 3;
    } else {
      ctxIdx = ctxIdxOffset + 3;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 0) ? 2 : 1;
    }
  } else {
    RET(decode_mb_type_in_I_slices(17, synElVal));
    synElVal += 5;
  }

  return 0;
}

int Cabac::decode_mb_type_in_B_slices(int32_t &synElVal) {
  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0;
  int32_t binVal = 0;
  int32_t ctxIdx = 0;
  int32_t bypassFlag = 0;

  RET((slice_type % 5) != SLICE_B);

  ctxIdxOffset = 27;
  RET(derivation_ctxIdxInc_for_mb_type(ctxIdxOffset, ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = ctxIdxOffset + 3;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));
    if (binVal == 0) {
      ctxIdx = ctxIdxOffset + 5;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 0) ? 1 : 2;
    } else {
      ctxIdx = ctxIdxOffset + 4;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      if (binVal == 0) {
        ctxIdx = ctxIdxOffset + 5;
        RET(decodeBin(bypassFlag, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = ctxIdxOffset + 5;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 3 : 4;
          } else {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 5 : 6;
          }
        } else {
          ctxIdx = ctxIdxOffset + 5;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 7 : 8;
          } else {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 9 : 10;
          }
        }
      } else {
        ctxIdx = ctxIdxOffset + 5;
        RET(decodeBin(bypassFlag, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = ctxIdxOffset + 5;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + 5;
              RET(decodeBin(bypassFlag, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 12 : 13;
            } else {
              ctxIdx = ctxIdxOffset + 5;
              RET(decodeBin(bypassFlag, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 14 : 15;
            }
          } else {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + 5;
              RET(decodeBin(bypassFlag, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 16 : 17;
            } else {
              ctxIdx = ctxIdxOffset + 5;
              RET(decodeBin(bypassFlag, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 18 : 19;
            }
          }
        } else {
          ctxIdx = ctxIdxOffset + 5;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            if (binVal == 0) {
              ctxIdx = ctxIdxOffset + 5;
              RET(decodeBin(bypassFlag, ctxIdx, binVal));
              synElVal = (binVal == 0) ? 20 : 21;
            } else {
              ctxIdxOffset = 32;
              RET(decode_mb_type_in_I_slices(ctxIdxOffset, synElVal));
              synElVal += 23;
            }
          } else {
            ctxIdx = ctxIdxOffset + 5;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 11 : 22;
          }
        }
      }
    }
  }

  return 0;
}

// 9.3.3.1.1.3 Derivation process of ctxIdxInc for the syntax element mb_type
int Cabac::derivation_ctxIdxInc_for_mb_type(int32_t ctxIdxOffset,
                                            int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;

  int32_t mbAddrA = 0, mbAddrB = 0;

  // 6.4.11.1 Derivation process for neighbouring macroblocks
  RET(picture.derivation_for_neighbouring_macroblocks(
      MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, 0));

  int32_t condTermFlagA = 1, condTermFlagB = 1;

  if (mbAddrA < 0 ||
      (ctxIdxOffset == 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type == SI) ||
      (ctxIdxOffset == 3 &&
       picture.m_mbs[mbAddrA].m_name_of_mb_type == I_NxN) ||
      (ctxIdxOffset == 27 &&
       (picture.m_mbs[mbAddrA].m_name_of_mb_type == B_Skip ||
        picture.m_mbs[mbAddrA].m_name_of_mb_type == B_Direct_16x16)))
    condTermFlagA = 0;

  if (mbAddrB < 0 ||
      (ctxIdxOffset == 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type == SI) ||
      (ctxIdxOffset == 3 &&
       picture.m_mbs[mbAddrB].m_name_of_mb_type == I_NxN) ||
      (ctxIdxOffset == 27 &&
       (picture.m_mbs[mbAddrB].m_name_of_mb_type == B_Skip ||
        picture.m_mbs[mbAddrB].m_name_of_mb_type == B_Direct_16x16)))
    condTermFlagB = 0;

  ctxIdxInc = condTermFlagA + condTermFlagB;

  return 0;
}

int Cabac::decode_sub_mb_type(int32_t &synElVal) {
  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int ret = 0;
  if ((slice_type % 5) == SLICE_P || (slice_type % 5) == SLICE_SP)
    ret = decode_sub_mb_type_in_P_SP_slices(synElVal);
  else if ((slice_type % 5) == SLICE_B)
    ret = decode_sub_mb_type_in_B_slices(synElVal);
  else
    RET(-1);
  RET(ret);
  return 0;
}

int Cabac::decode_sub_mb_type_in_P_SP_slices(int32_t &synElVal) {
  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  RET((slice_type % 5) != SLICE_P && (slice_type % 5) != SLICE_SP);

  ctxIdxOffset = 21;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 1)
    synElVal = 0;
  else if (binVal == 0) {
    ctxIdx = ctxIdxOffset + 1;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));
    if (binVal == 0)
      synElVal = 1;
    else {
      ctxIdx = ctxIdxOffset + 2;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 1) ? 2 : 3;
    }
  }

  return 0;
}

int Cabac::decode_sub_mb_type_in_B_slices(int32_t &synElVal) {

  H264_SLICE_TYPE slice_type =
      (H264_SLICE_TYPE)picture.m_slice->slice_header->slice_type;
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  RET((slice_type % 5) != SLICE_B);

  ctxIdxOffset = 36;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = ctxIdxOffset + 1;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));
    if (binVal == 0) {
      ctxIdx = ctxIdxOffset + 3;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 0) ? 1 : 2;
    } else {
      ctxIdx = ctxIdxOffset + 2;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      if (binVal == 0) {
        ctxIdx = ctxIdxOffset + 3;
        RET(decodeBin(bypassFlag, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = ctxIdxOffset + 3;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          synElVal = (binVal == 0) ? 3 : 4;
        } else {
          ctxIdx = ctxIdxOffset + 3;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          synElVal = (binVal == 0) ? 5 : 6;
        }
      } else {
        ctxIdx = ctxIdxOffset + 3;
        RET(decodeBin(bypassFlag, ctxIdx, binVal));
        if (binVal == 0) {
          ctxIdx = ctxIdxOffset + 3;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          if (binVal == 0) {
            ctxIdx = ctxIdxOffset + 3;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 7 : 8;
          } else {
            ctxIdx = ctxIdxOffset + 3;
            RET(decodeBin(bypassFlag, ctxIdx, binVal));
            synElVal = (binVal == 0) ? 9 : 10;
          }
        } else {
          ctxIdx = ctxIdxOffset + 3;
          RET(decodeBin(bypassFlag, ctxIdx, binVal));
          synElVal = (binVal == 0) ? 11 : 12;
        }
      }
    }
  }

  return 0;
}

// 9.3.3.1.1.4 Derivation process of ctxIdxInc for the syntax element coded_block_pattern
int Cabac::derivation_ctxIdxInc_for_coded_block_pattern(int32_t binIdx,
                                                        int32_t binValues,
                                                        int32_t ctxIdxOffset,
                                                        int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;

  if (ctxIdxOffset == 73) {
    int32_t luma8x8BlkIdx = binIdx;
    int32_t mbAddrA = 0, mbAddrB = 0;
    int32_t luma8x8BlkIdxA = 0, luma8x8BlkIdxB = 0;
    int32_t isChroma = 0;

    // 6.4.11.2 Derivation process for neighbouring 8x8 luma block
    RET(picture.derivation_for_neighbouring_8x8_luma_block(
        luma8x8BlkIdx, mbAddrA, mbAddrB, luma8x8BlkIdxA, luma8x8BlkIdxB,
        isChroma));

    int32_t condTermFlagA = 1, condTermFlagB = 1;

    //-------A-----------
    if (mbAddrA < 0 || picture.m_mbs[mbAddrA].m_name_of_mb_type == I_PCM ||
        (mbAddrA != picture.CurrMbAddr &&
         (picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
          picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip) &&
         ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> luma8x8BlkIdxA) &
          1) != 0) ||
        (mbAddrA == picture.CurrMbAddr &&
         ((binValues >> luma8x8BlkIdxA) & 0x01) != 0))
      condTermFlagA = 0;

    //-------B-----------
    if (mbAddrB < 0 || picture.m_mbs[mbAddrB].m_name_of_mb_type == I_PCM ||
        (mbAddrB != picture.CurrMbAddr &&
         (picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
          picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip) &&
         ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> luma8x8BlkIdxB) &
          1) != 0) ||
        (mbAddrB == picture.CurrMbAddr &&
         ((binValues >> luma8x8BlkIdxB) & 0x01) != 0))
      condTermFlagB = 0;

    ctxIdxInc = condTermFlagA + 2 * condTermFlagB;
  } else {
    int32_t mbAddrA = 0, mbAddrB = 0;
    const int32_t isChroma = 0;

    // 6.4.11.1 Derivation process for neighbouring macroblocks
    RET(picture.derivation_for_neighbouring_macroblocks(
        MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, isChroma));

    int32_t condTermFlagA = 1, condTermFlagB = 1;
    //-----------A----------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type == I_PCM)
      condTermFlagA = 1;
    else if (mbAddrA < 0 ||
             picture.m_mbs[mbAddrA].m_name_of_mb_type == P_Skip ||
             picture.m_mbs[mbAddrA].m_name_of_mb_type == B_Skip ||
             (binIdx == 0 &&
              picture.m_mbs[mbAddrA].CodedBlockPatternChroma == 0) ||
             (binIdx == 1 &&
              picture.m_mbs[mbAddrA].CodedBlockPatternChroma != 2))
      condTermFlagA = 0;

    //-----------B----------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type == I_PCM)
      condTermFlagB = 1;
    else if (mbAddrB < 0 ||
             picture.m_mbs[mbAddrB].m_name_of_mb_type == P_Skip ||
             picture.m_mbs[mbAddrB].m_name_of_mb_type == B_Skip ||
             (binIdx == 0 &&
              picture.m_mbs[mbAddrB].CodedBlockPatternChroma == 0) ||
             (binIdx == 1 &&
              picture.m_mbs[mbAddrB].CodedBlockPatternChroma != 2))
      condTermFlagB = 0;

    ctxIdxInc = condTermFlagA + 2 * condTermFlagB + ((binIdx == 1) ? 4 : 0);
  }

  return 0;
}

//9.3.3.1.1.5 Derivation process of ctxIdxInc for the syntax element mb_qp_delta
int Cabac::decode_mb_qp_delta(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binIdx = -1, binVal = 0;
  int32_t bypassFlag = 0;
  int32_t bit_depth_luma = 8;

  ctxIdxOffset = 60;

  RET(derivation_ctxIdxInc_for_mb_qp_delta(ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = ctxIdxOffset + 2;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));

    int mb_qp_max = 51 + 6 * (bit_depth_luma - 8);
    binIdx = 1;

    while (binVal == 1) {
      ctxIdx = ctxIdxOffset + 3;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      binIdx++;
      if (binIdx > 2 * mb_qp_max) {
        std::cerr << "An error occurred on binIdx:" << binIdx << " > "
                  << 2 * mb_qp_max << " ," << __FUNCTION__ << "():" << __LINE__
                  << std::endl;
        binIdx = 2 * mb_qp_max;
        break;
      }
    }

    if (binIdx & 0x01)
      binIdx = (binIdx + 1) >> 1;
    else
      binIdx = -((binIdx + 1) >> 1);

    synElVal = binIdx;
  }

  return 0;
}

// 9.3.3.1.1.5 Derivation process of ctxIdxInc for the syntax element
int Cabac::derivation_ctxIdxInc_for_mb_qp_delta(int32_t &ctxIdxInc) {
  const SliceHeader *header = picture.m_slice->slice_header;

  /* 令 prevMbAddr 为按解码顺序位于当前宏块之前的宏块的宏块地址。当当前宏块是切片的第一个宏块时，prevMbAddr被标记为不可用。 */
  int32_t prevMbAddr = picture.CurrMbAddr - 1;
  const MacroBlock &pre_mb = picture.m_mbs[prevMbAddr];

  /* Slice的第一个宏块地址推导如下：
– 如果MbaffFrameFlag等于0，则first_mb_in_slice是切片中第一个宏块的宏块地址，并且first_mb_in_slice应在0到PicSizeInMbs - 1的范围内（包括0和PicSizeInMbs - 1）。
– 否则（MbaffFrameFlag等于1），first_mb_in_slice * 2是第一个宏块的宏块地址*/
  int32_t FirstMbAddrOfSlice =
      header->first_slice_segment_in_pic_flag * (1 + header->MbaffFrameFlag);

  if (picture.CurrMbAddr == FirstMbAddrOfSlice) prevMbAddr = -1;

  /* 变量 ctxIdxInc 的推导如下： 
   * – 如果以下任一条件成立，则 ctxIdxInc 设置为等于 0： 
     * – prevMbAddr 不可用或宏块 prevMbAddr 的 mb_type 等于 P_Skip 或 B_Skip， 
     * – 宏块 prevMbAddr 的 mb_type 为等于 I_PCM， 
     * – 宏块 prevMbAddr 未在 Intra_16x16 宏块预测模式下编码，且宏块 prevMbAddr 的 CodedBlockPatternLuma 和 CodedBlockPatternChroma 均等于 0， 
     * – 宏块 prevMbAddr 的 mb_qp_delta 等于 0。 
   * – 否则，将 ctxIdxInc 设置为等于 1 。 */
  ctxIdxInc = 1;
  if (prevMbAddr < 0 || pre_mb.m_name_of_mb_type == P_Skip ||
      pre_mb.m_name_of_mb_type == B_Skip || pre_mb.m_name_of_mb_type == I_PCM ||
      (pre_mb.m_mb_pred_mode != Intra_16x16 &&
       pre_mb.CodedBlockPatternLuma == 0 &&
       pre_mb.CodedBlockPatternChroma == 0) ||
      pre_mb.mb_qp_delta == 0)
    ctxIdxInc = 0;

  return 0;
}

int Cabac::decode_ref_idx_lX(int32_t ref_idx_flag, int32_t mbPartIdx,
                             int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binIdx = -1, binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 54;

  int32_t is_ref_idx_10 = (ref_idx_flag == 0) ? 1 : 0;
  RET(derivation_ctxIdxInc_for_ref_idx_lX(is_ref_idx_10, mbPartIdx, ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = ctxIdxOffset + 4;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));
    binIdx = 1;
    ctxIdx = ctxIdxOffset + 5;
    while (binVal == 1) {
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      binIdx++;
      RET(binIdx > 32);
    }
    synElVal = binIdx;
  }

  return 0;
}

// 9.3.3.1.1.6 Derivation process of ctxIdxInc for the syntax elements ref_idx_l0 and ref_idx_l1
int Cabac::derivation_ctxIdxInc_for_ref_idx_lX(int32_t is_ref_idx_10,
                                               int32_t mbPartIdx,
                                               int32_t &ctxIdxInc) {

  const MacroBlock &mb = picture.m_mbs[picture.CurrMbAddr];
  const int32_t isChroma = 0;

  int32_t mbAddrN_A = 0, mbPartIdxN_A = 0, subMbPartIdxN_A = 0;
  int32_t mbAddrN_B = 0, mbPartIdxN_B = 0, subMbPartIdxN_B = 0;
  int32_t subMbPartIdx = 0;
  H264_MB_TYPE currSubMbType = mb.m_name_of_sub_mb_type[mbPartIdx];

  int32_t MbPartWidth = mb.MbPartWidth;
  int32_t MbPartHeight = mb.MbPartHeight;
  int32_t SubMbPartWidth = mb.SubMbPartWidth[mbPartIdx];
  int32_t SubMbPartHeight = mb.SubMbPartHeight[mbPartIdx];

  int32_t x = InverseRasterScan(mbPartIdx, MbPartWidth, MbPartHeight, 16, 0);
  int32_t y = InverseRasterScan(mbPartIdx, MbPartWidth, MbPartHeight, 16, 1);

  int32_t xS = 0, yS = 0;
  if (mb.m_name_of_mb_type == P_8x8 || mb.m_name_of_mb_type == P_8x8ref0 ||
      mb.m_name_of_mb_type == B_8x8) {
    xS = InverseRasterScan(subMbPartIdx, SubMbPartWidth, SubMbPartHeight, 8, 0);
    yS = InverseRasterScan(subMbPartIdx, SubMbPartWidth, SubMbPartHeight, 8, 1);
  }

  RET(picture.derivation_neighbouring_partitions(
      x + xS - 1, y + yS + 0, mbPartIdx, currSubMbType, subMbPartIdx, isChroma,
      mbAddrN_A, mbPartIdxN_A, subMbPartIdxN_A));

  RET(picture.derivation_neighbouring_partitions(
      x + xS + 0, y + yS - 1, mbPartIdx, currSubMbType, subMbPartIdx, isChroma,
      mbAddrN_B, mbPartIdxN_B, subMbPartIdxN_B));

  int32_t refIdxZeroFlagA = 0, refIdxZeroFlagB = 0;
  int32_t predModeEqualFlagA = 0, predModeEqualFlagB = 0;
  int32_t condTermFlagA = 0, condTermFlagB = 0;

  //------------A--------------------
  if (mbAddrN_A >= 0) {
    if (mb.MbaffFrameFlag && mb.mb_field_decoding_flag == 0 &&
        picture.m_mbs[mbAddrN_A].mb_field_decoding_flag) {
      if (is_ref_idx_10 == 1)
        refIdxZeroFlagA =
            (picture.m_mbs[mbAddrN_A].ref_idx_l0[mbPartIdxN_A] <= 1);
      else
        refIdxZeroFlagA =
            (picture.m_mbs[mbAddrN_A].ref_idx_l1[mbPartIdxN_A] <= 1);
    } else {
      if (is_ref_idx_10 == 1)
        refIdxZeroFlagA =
            (picture.m_mbs[mbAddrN_A].ref_idx_l0[mbPartIdxN_A] <= 0);
      else
        refIdxZeroFlagA =
            (picture.m_mbs[mbAddrN_A].ref_idx_l1[mbPartIdxN_A] <= 0);
    }

    if (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Direct_16x16 ||
        picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Skip)
      predModeEqualFlagA = 0;
    else if (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == P_8x8 ||
             picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_8x8) {
      int32_t NumSubMbPart = 0, SubMbPartWidth = 0, SubMbPartHeight = 0;
      H264_MB_PART_PRED_MODE SubMbPredMode = MB_PRED_MODE_NA;

      RET(MacroBlock::SubMbPredMode(
          picture.m_slice->slice_header->slice_type,
          picture.m_mbs[mbAddrN_A].sub_mb_type[mbPartIdxN_A], NumSubMbPart,
          SubMbPredMode, SubMbPartWidth, SubMbPartHeight));

      if (((is_ref_idx_10 == 1 && SubMbPredMode != Pred_L0) ||
           (is_ref_idx_10 == 0 && SubMbPredMode != Pred_L1)) &&
          SubMbPredMode != BiPred)
        predModeEqualFlagA = 0;
      else
        predModeEqualFlagA = 1;
    } else {
      H264_MB_PART_PRED_MODE mb_pred_mode = MB_PRED_MODE_NA;

      RET(MacroBlock::MbPartPredMode(
          picture.m_mbs[mbAddrN_A].m_name_of_mb_type, mbPartIdxN_A,
          picture.m_mbs[mbAddrN_A].transform_size_8x8_flag, mb_pred_mode));

      if (((is_ref_idx_10 == 1 && mb_pred_mode != Pred_L0) ||
           (is_ref_idx_10 == 0 && mb_pred_mode != Pred_L1)) &&
          mb_pred_mode != BiPred)
        predModeEqualFlagA = 0;
      else
        predModeEqualFlagA = 1;
    }
  } else
    predModeEqualFlagA = 0;

  if (mbAddrN_A < 0 ||
      (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == P_Skip ||
       picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Skip) ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrN_A].m_mb_pred_mode) ||
      predModeEqualFlagA == 0 || refIdxZeroFlagA == 1)
    condTermFlagA = 0;
  else
    condTermFlagA = 1;

  //------------B--------------------
  if (mbAddrN_B >= 0) {
    if (mb.MbaffFrameFlag == 1 && mb.mb_field_decoding_flag == 0 &&
        picture.m_mbs[mbAddrN_B].mb_field_decoding_flag == 1) {
      if (is_ref_idx_10 == 1)
        refIdxZeroFlagB =
            (picture.m_mbs[mbAddrN_B].ref_idx_l0[mbPartIdxN_B] <= 1);
      else
        refIdxZeroFlagB =
            (picture.m_mbs[mbAddrN_B].ref_idx_l1[mbPartIdxN_B] <= 1);
    } else {
      if (is_ref_idx_10 == 1)
        refIdxZeroFlagB =
            (picture.m_mbs[mbAddrN_B].ref_idx_l0[mbPartIdxN_B] <= 0);
      else
        refIdxZeroFlagB =
            (picture.m_mbs[mbAddrN_B].ref_idx_l1[mbPartIdxN_B] <= 0);
    }

    if (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Direct_16x16 ||
        picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Skip) {
      predModeEqualFlagB = 0;
    } else if (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == P_8x8 ||
               picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_8x8) {
      int32_t NumSubMbPart = 0, SubMbPartWidth = 0, SubMbPartHeight = 0;
      H264_MB_PART_PRED_MODE SubMbPredMode = MB_PRED_MODE_NA;

      RET(MacroBlock::SubMbPredMode(
          picture.m_slice->slice_header->slice_type,
          picture.m_mbs[mbAddrN_B].sub_mb_type[mbPartIdxN_B], NumSubMbPart,
          SubMbPredMode, SubMbPartWidth, SubMbPartHeight));

      if (((is_ref_idx_10 == 1 && SubMbPredMode != Pred_L0) ||
           (is_ref_idx_10 == 0 && SubMbPredMode != Pred_L1)) &&
          SubMbPredMode != BiPred)
        predModeEqualFlagB = 0;
      else
        predModeEqualFlagB = 1;
    } else {
      H264_MB_PART_PRED_MODE mb_pred_mode = MB_PRED_MODE_NA;

      RET(MacroBlock::MbPartPredMode(
          picture.m_mbs[mbAddrN_B].m_name_of_mb_type, mbPartIdxN_B,
          picture.m_mbs[mbAddrN_B].transform_size_8x8_flag, mb_pred_mode));

      if (((is_ref_idx_10 == 1 && mb_pred_mode != Pred_L0) ||
           (is_ref_idx_10 == 0 && mb_pred_mode != Pred_L1)) &&
          mb_pred_mode != BiPred)
        predModeEqualFlagB = 0;
      else
        predModeEqualFlagB = 1;
    }
  } else
    predModeEqualFlagB = 0;

  if (mbAddrN_B < 0 ||
      (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == P_Skip ||
       picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Skip) ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrN_B].m_mb_pred_mode) ||
      predModeEqualFlagB == 0 || refIdxZeroFlagB == 1)
    condTermFlagB = 0;
  else
    condTermFlagB = 1;

  ctxIdxInc = condTermFlagA + 2 * condTermFlagB;

  return 0;
}

// 9.3.3.1.1.7 Derivation process of ctxIdxInc for the syntax elements mvd_l0 and mvd_l1
int Cabac::derivation_ctxIdxInc_for_mvd_lX(int32_t is_mvd_10, int32_t mbPartIdx,
                                           int32_t subMbPartIdx,
                                           int32_t isChroma,
                                           int32_t ctxIdxOffset,
                                           int32_t &ctxIdxInc) {

  const MacroBlock &mb = picture.m_mbs[picture.CurrMbAddr];
  const int32_t MbPartWidth = mb.MbPartWidth;
  const int32_t MbPartHeight = mb.MbPartHeight;
  const int32_t SubMbPartWidth = mb.SubMbPartWidth[mbPartIdx];
  const int32_t SubMbPartHeight = mb.SubMbPartHeight[mbPartIdx];

  int32_t x = InverseRasterScan(mbPartIdx, MbPartWidth, MbPartHeight, 16, 0);
  int32_t y = InverseRasterScan(mbPartIdx, MbPartWidth, MbPartHeight, 16, 1);

  int32_t xS = 0, yS = 0;
  if (mb.m_name_of_mb_type == P_8x8 || mb.m_name_of_mb_type == P_8x8ref0 ||
      mb.m_name_of_mb_type == B_8x8) {
    xS = InverseRasterScan(subMbPartIdx, SubMbPartWidth, SubMbPartHeight, 8, 0);
    yS = InverseRasterScan(subMbPartIdx, SubMbPartWidth, SubMbPartHeight, 8, 1);
  }

  H264_MB_TYPE currSubMbType = mb.m_name_of_sub_mb_type[mbPartIdx];

  int32_t mbAddrN_A = 0, mbPartIdxN_A = 0, subMbPartIdxN_A = 0;
  int32_t mbAddrN_B = 0, mbPartIdxN_B = 0, subMbPartIdxN_B = 0;
  RET(picture.derivation_neighbouring_partitions(
      x + xS - 1, y + yS + 0, mbPartIdx, currSubMbType, subMbPartIdx, isChroma,
      mbAddrN_A, mbPartIdxN_A, subMbPartIdxN_A));
  RET(picture.derivation_neighbouring_partitions(
      x + xS + 0, y + yS - 1, mbPartIdx, currSubMbType, subMbPartIdx, isChroma,
      mbAddrN_B, mbPartIdxN_B, subMbPartIdxN_B));

  int32_t compIdx = (ctxIdxOffset == 40) ? 0 : 1;
  int32_t predModeEqualFlagA = 0, predModeEqualFlagB = 0;
  int32_t absMvdCompA = 0, absMvdCompB = 0;

  //------------------A-------------------------
  if (mbAddrN_A >= 0 &&
      (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Direct_16x16 ||
       picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Skip))
    predModeEqualFlagA = 0;
  else if (mbAddrN_A >= 0 &&
           (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == P_8x8 ||
            picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_8x8)) {
    int32_t NumSubMbPart = 0, SubMbPartWidth = 0, SubMbPartHeight = 0;
    H264_MB_PART_PRED_MODE SubMbPredMode = MB_PRED_MODE_NA;

    RET(MacroBlock::SubMbPredMode(
        picture.m_slice->slice_header->slice_type,
        picture.m_mbs[mbAddrN_A].sub_mb_type[mbPartIdxN_A], NumSubMbPart,
        SubMbPredMode, SubMbPartWidth, SubMbPartHeight));

    if (((is_mvd_10 == 1 && SubMbPredMode != Pred_L0) ||
         (is_mvd_10 == 0 && SubMbPredMode != Pred_L1)) &&
        SubMbPredMode != BiPred)
      predModeEqualFlagA = 0;
    else
      predModeEqualFlagA = 1;
  } else if (mbAddrN_A >= 0) {
    H264_MB_PART_PRED_MODE mb_pred_mode = MB_PRED_MODE_NA;

    RET(MacroBlock::MbPartPredMode(
        picture.m_mbs[mbAddrN_A].m_name_of_mb_type, mbPartIdxN_A,
        picture.m_mbs[mbAddrN_A].transform_size_8x8_flag, mb_pred_mode));

    if (((is_mvd_10 == 1 && mb_pred_mode != Pred_L0) ||
         (is_mvd_10 == 0 && mb_pred_mode != Pred_L1)) &&
        mb_pred_mode != BiPred)
      predModeEqualFlagA = 0;
    else
      predModeEqualFlagA = 1;
  } else
    predModeEqualFlagA = 0;

  if (mbAddrN_A < 0 ||
      (picture.m_mbs[mbAddrN_A].m_name_of_mb_type == P_Skip ||
       picture.m_mbs[mbAddrN_A].m_name_of_mb_type == B_Skip) ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrN_A].m_mb_pred_mode) ||
      predModeEqualFlagA == 0)
    absMvdCompA = 0;
  else {
    if (compIdx == 1 && mb.MbaffFrameFlag == 1 &&
        mb.mb_field_decoding_flag == 0 &&
        picture.m_mbs[mbAddrN_A].mb_field_decoding_flag == 1) {
      if (is_mvd_10 == 1)
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l0[mbPartIdxN_A][subMbPartIdxN_A][compIdx]) *
                      2;
      else
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l1[mbPartIdxN_A][subMbPartIdxN_A][compIdx]) *
                      2;
    } else if (compIdx == 1 && mb.MbaffFrameFlag == 1 &&
               mb.mb_field_decoding_flag == 1 &&
               picture.m_mbs[mbAddrN_A].mb_field_decoding_flag == 0) {
      if (is_mvd_10 == 1)
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l0[mbPartIdxN_A][subMbPartIdxN_A][compIdx]) /
                      2;
      else
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l1[mbPartIdxN_A][subMbPartIdxN_A][compIdx]) /
                      2;
    } else {
      if (is_mvd_10 == 1)
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l0[mbPartIdxN_A][subMbPartIdxN_A][compIdx]);
      else
        absMvdCompA = ABS(picture.m_mbs[mbAddrN_A]
                              .mvd_l1[mbPartIdxN_A][subMbPartIdxN_A][compIdx]);
    }
  }

  //------------------B-------------------------
  if (mbAddrN_B >= 0 &&
      (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Direct_16x16 ||
       picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Skip)) {
    predModeEqualFlagB = 0;
  } else if (mbAddrN_B >= 0 &&
             (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == P_8x8 ||
              picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_8x8)) {
    int32_t NumSubMbPart = 0, SubMbPartWidth = 0, SubMbPartHeight = 0;
    H264_MB_PART_PRED_MODE SubMbPredMode = MB_PRED_MODE_NA;

    RET(MacroBlock::SubMbPredMode(
        picture.m_slice->slice_header->slice_type,
        picture.m_mbs[mbAddrN_B].sub_mb_type[mbPartIdxN_B], NumSubMbPart,
        SubMbPredMode, SubMbPartWidth, SubMbPartHeight));

    if (((is_mvd_10 == 1 && SubMbPredMode != Pred_L0) ||
         (is_mvd_10 == 0 && SubMbPredMode != Pred_L1)) &&
        SubMbPredMode != BiPred)
      predModeEqualFlagB = 0;
    else
      predModeEqualFlagB = 1;
  } else if (mbAddrN_B >= 0) {
    H264_MB_PART_PRED_MODE mb_pred_mode = MB_PRED_MODE_NA;

    RET(MacroBlock::MbPartPredMode(
        picture.m_mbs[mbAddrN_B].m_name_of_mb_type, mbPartIdxN_B,
        picture.m_mbs[mbAddrN_B].transform_size_8x8_flag, mb_pred_mode));

    if (((is_mvd_10 == 1 && mb_pred_mode != Pred_L0) ||
         (is_mvd_10 == 0 && mb_pred_mode != Pred_L1)) &&
        mb_pred_mode != BiPred)
      predModeEqualFlagB = 0;
    else
      predModeEqualFlagB = 1;
  } else
    predModeEqualFlagB = 0;

  if (mbAddrN_B < 0 ||
      (picture.m_mbs[mbAddrN_B].m_name_of_mb_type == P_Skip ||
       picture.m_mbs[mbAddrN_B].m_name_of_mb_type == B_Skip) ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrN_B].m_mb_pred_mode) ||
      predModeEqualFlagB == 0)
    absMvdCompB = 0;
  else {
    if (compIdx == 1 && mb.MbaffFrameFlag && mb.mb_field_decoding_flag == 0 &&
        picture.m_mbs[mbAddrN_B].mb_field_decoding_flag) {
      if (is_mvd_10 == 1)
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l0[mbPartIdxN_B][subMbPartIdxN_B][compIdx]) *
                      2;
      else
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l1[mbPartIdxN_B][subMbPartIdxN_B][compIdx]) *
                      2;
    } else if (compIdx == 1 && mb.MbaffFrameFlag && mb.mb_field_decoding_flag &&
               picture.m_mbs[mbAddrN_B].mb_field_decoding_flag == 0) {
      if (is_mvd_10 == 1)
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l0[mbPartIdxN_B][subMbPartIdxN_B][compIdx]) /
                      2;
      else
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l1[mbPartIdxN_B][subMbPartIdxN_B][compIdx]) /
                      2;
    } else {
      if (is_mvd_10 == 1)
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l0[mbPartIdxN_B][subMbPartIdxN_B][compIdx]);
      else
        absMvdCompB = ABS(picture.m_mbs[mbAddrN_B]
                              .mvd_l1[mbPartIdxN_B][subMbPartIdxN_B][compIdx]);
    }
  }

  if ((absMvdCompA + absMvdCompB) < 3)
    ctxIdxInc = 0;
  else if ((absMvdCompA + absMvdCompB) > 32)
    ctxIdxInc = 2;
  else
    ctxIdxInc = 1;

  return 0;
}

// 9.3.3.1.1.8 Derivation process of ctxIdxInc for the syntax element intra_chroma_pred_mode
int Cabac::decode_intra_chroma_pred_mode(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 64;

  RET(derivation_ctxIdxInc_for_intra_chroma_pred_mode(ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));

  if (binVal == 0)
    synElVal = 0;
  else {
    ctxIdx = ctxIdxOffset + 3;
    RET(decodeBin(bypassFlag, ctxIdx, binVal));

    if (binVal == 0)
      synElVal = 1;
    else {
      ctxIdx = ctxIdxOffset + 3;
      RET(decodeBin(bypassFlag, ctxIdx, binVal));
      synElVal = (binVal == 0) ? 2 : 3;
    }
  }

  return 0;
}

// 9.3.3.1.1.8 Derivation process of ctxIdxInc for the syntax element intra_chroma_pred_mode
int Cabac::derivation_ctxIdxInc_for_intra_chroma_pred_mode(int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;
  const int32_t isChroma = 0;

  int32_t mbAddrA = 0, mbAddrB = 0;
  RET(picture.derivation_for_neighbouring_macroblocks(
      MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, isChroma));

  int32_t condTermFlagA = 1, condTermFlagB = 1;
  //----------A------------
  if (mbAddrA < 0 ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrA].m_mb_pred_mode) ==
          false ||
      picture.m_mbs[mbAddrA].m_name_of_mb_type == I_PCM ||
      picture.m_mbs[mbAddrA].intra_chroma_pred_mode == 0)
    condTermFlagA = 0;

  //----------B------------
  if (mbAddrB < 0 ||
      IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrB].m_mb_pred_mode) ==
          false ||
      picture.m_mbs[mbAddrB].m_name_of_mb_type == I_PCM ||
      picture.m_mbs[mbAddrB].intra_chroma_pred_mode == 0)
    condTermFlagB = 0;

  ctxIdxInc = condTermFlagA + condTermFlagB;

  return 0;
}

// 9.3.3.1.1.9 Derivation process of ctxIdxInc for the syntax element coded_block_flag
int Cabac::decode_coded_block_flag(MB_RESIDUAL_LEVEL mb_block_level,
                                   int32_t BlkIdx, int32_t iCbCr,
                                   int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;
  int32_t ctxBlockCat = 0;

  ctxBlockCat = mb_block_level;

  if (ctxBlockCat < 5)
    ctxIdxOffset = 85;
  else if (ctxBlockCat > 5 && ctxBlockCat < 9)
    ctxIdxOffset = 460;
  else if (ctxBlockCat > 9 && ctxBlockCat < 13)
    ctxIdxOffset = 472;
  else
    ctxIdxOffset = 1012;

  const int32_t ctxIdxBlockCatOffset_arr[14] = {0, 4, 8, 12, 16, 0, 0,
                                                4, 8, 4, 0,  4,  8, 8};
  int32_t ctxIdxBlockCatOffset = ctxIdxBlockCatOffset_arr[ctxBlockCat];

  RET(derivation_ctxIdxInc_for_coded_block_flag(ctxBlockCat, BlkIdx, iCbCr,
                                                ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxBlockCatOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;
  return 0;
}

// 9.3.3.1.1.9 Derivation process of ctxIdxInc for the syntax element coded_block_flag ctxIdxInc( ctxBlockCat )
int Cabac::derivation_ctxIdxInc_for_coded_block_flag(int32_t ctxBlockCat,
                                                     int32_t BlkIdx,
                                                     int32_t iCbCr,
                                                     int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;

  int32_t mbAddrA = 0, mbAddrB = 0;
  int32_t transBlockA = -1, transBlockB = -1;
  int32_t transBlockA_coded_block_flag = 0, transBlockB_coded_block_flag = 0;

  if (ctxBlockCat == 0 || ctxBlockCat == 6 || ctxBlockCat == 10) {
    int32_t isChroma = (iCbCr < 0) ? 0 : 1;
    RET(picture.derivation_for_neighbouring_macroblocks(
        MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, isChroma));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_mb_pred_mode == Intra_16x16) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_DC_pattern >> (iCbCr + 1)) &
          1;
    } else
      transBlockA = -1;
    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_mb_pred_mode == Intra_16x16) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_DC_pattern >> (iCbCr + 1)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 1 || ctxBlockCat == 2) {
    // 6.4.11.4 Derivation process for neighbouring 4x4 luma blocks
    int32_t luma4x4BlkIdx = BlkIdx, luma4x4BlkIdxA = 0, luma4x4BlkIdxB = 0;
    int32_t isChroma = (iCbCr < 0) ? 0 : 1;

    RET(picture.derivation_for_neighbouring_4x4_luma_blocks(
        luma4x4BlkIdx, mbAddrA, mbAddrB, luma4x4BlkIdxA, luma4x4BlkIdxB,
        isChroma));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >>
          (luma4x4BlkIdxA >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 0) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma4x4BlkIdxA)) &
          1;
    } else if (mbAddrA >= 0 &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >>
                 (luma4x4BlkIdxA >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma4x4BlkIdxA >> 2)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >>
          (luma4x4BlkIdxB >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 0) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma4x4BlkIdxB)) &
          1;
    } else if (mbAddrB >= 0 &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >>
                 (luma4x4BlkIdxB >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma4x4BlkIdxB >> 2)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 3) {
    int32_t isChroma = 1;
    RET(picture.derivation_for_neighbouring_macroblocks(
        MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, isChroma));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        picture.m_mbs[mbAddrA].CodedBlockPatternChroma != 0) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_DC_pattern >> (iCbCr + 1)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        picture.m_mbs[mbAddrB].CodedBlockPatternChroma != 0) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_DC_pattern >> (iCbCr + 1)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 4) {
    int32_t chroma4x4BlkIdx = BlkIdx, chroma4x4BlkIdxA = 0,
            chroma4x4BlkIdxB = 0;

    RET(picture.derivation_for_neighbouring_4x4_chroma_blocks(
        chroma4x4BlkIdx, mbAddrA, mbAddrB, chroma4x4BlkIdxA, chroma4x4BlkIdxB));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        picture.m_mbs[mbAddrA].CodedBlockPatternChroma == 2) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (chroma4x4BlkIdxA)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        picture.m_mbs[mbAddrB].CodedBlockPatternChroma == 2) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (chroma4x4BlkIdxB)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 5) {
    int32_t luma8x8BlkIdx = BlkIdx, luma8x8BlkIdxA = 0, luma8x8BlkIdxB = 0;
    int32_t isChroma = 0;

    RET(picture.derivation_for_neighbouring_8x8_luma_block(
        luma8x8BlkIdx, mbAddrA, mbAddrB, luma8x8BlkIdxA, luma8x8BlkIdxB,
        isChroma));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> luma8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma8x8BlkIdxA)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> luma8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (luma8x8BlkIdxB)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 7 || ctxBlockCat == 8) {
    int32_t cb4x4BlkIdx = BlkIdx, cb4x4BlkIdxA = 0, cb4x4BlkIdxB = 0;

    RET(picture.derivation_for_neighbouring_4x4_chroma_blocks(
        cb4x4BlkIdx, mbAddrA, mbAddrB, cb4x4BlkIdxA, cb4x4BlkIdxB));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> (cb4x4BlkIdxA >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 0) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb4x4BlkIdxA)) &
          1;
    } else if (mbAddrA >= 0 &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >>
                 (cb4x4BlkIdxA >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb4x4BlkIdxA >> 2)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> (cb4x4BlkIdxB >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 0) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb4x4BlkIdxB)) &
          1;
    } else if (mbAddrB >= 0 &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >>
                 (cb4x4BlkIdxB >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb4x4BlkIdxB >> 2)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 9) {
    int32_t cb8x8BlkIdx = BlkIdx, cb8x8BlkIdxA = 0, cb8x8BlkIdxB = 0;

    RET(picture.derivation_for_neighbouring_8x8_chroma_blocks_for_YUV444(
        cb8x8BlkIdx, mbAddrA, mbAddrB, cb8x8BlkIdxA, cb8x8BlkIdxB));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> cb8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb8x8BlkIdxA)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> cb8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cb8x8BlkIdxB)) &
          1;
    } else
      transBlockB = -1;
  } else if (ctxBlockCat == 11 || ctxBlockCat == 12) {
    int32_t cr4x4BlkIdx = BlkIdx, cr4x4BlkIdxA = 0, cr4x4BlkIdxB = 0;

    RET(picture.derivation_for_neighbouring_4x4_chroma_blocks(
        cr4x4BlkIdx, mbAddrA, mbAddrB, cr4x4BlkIdxA, cr4x4BlkIdxB));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> (cr4x4BlkIdxA >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 0) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr4x4BlkIdxA)) &
          1;
    } else if (mbAddrA >= 0 &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >>
                 (cr4x4BlkIdxA >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr4x4BlkIdxA >> 2)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> (cr4x4BlkIdxB >> 2)) &
         1) != 0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 0) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr4x4BlkIdxB)) &
          1;
    } else if (mbAddrB >= 0 &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
               picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
               ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >>
                 (cr4x4BlkIdxB >> 2)) &
                1) != 0 &&
               picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr4x4BlkIdxB >> 2)) &
          1;
    } else
      transBlockB = -1;
  } else {
    int32_t cr8x8BlkIdx = BlkIdx, cr8x8BlkIdxA = 0, cr8x8BlkIdxB = 0;

    RET(picture.derivation_for_neighbouring_8x8_chroma_blocks_for_YUV444(
        cr8x8BlkIdx, mbAddrA, mbAddrB, cr8x8BlkIdxA, cr8x8BlkIdxB));

    //----------A------------
    if (mbAddrA >= 0 && picture.m_mbs[mbAddrA].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrA].CodedBlockPatternLuma >> cr8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrA].transform_size_8x8_flag == 1) {
      transBlockA = 1;
      transBlockA_coded_block_flag =
          (picture.m_mbs[mbAddrA].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr8x8BlkIdxA)) &
          1;
    } else
      transBlockA = -1;

    //----------B------------
    if (mbAddrB >= 0 && picture.m_mbs[mbAddrB].m_name_of_mb_type != P_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != B_Skip &&
        picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM &&
        ((picture.m_mbs[mbAddrB].CodedBlockPatternLuma >> cr8x8BlkIdx) & 1) !=
            0 &&
        picture.m_mbs[mbAddrB].transform_size_8x8_flag == 1) {
      transBlockB = 1;
      transBlockB_coded_block_flag =
          (picture.m_mbs[mbAddrB].coded_block_flag_AC_pattern[iCbCr + 1] >>
           (cr8x8BlkIdxB)) &
          1;
    } else
      transBlockB = -1;
  }

  int32_t condTermFlagA = 0, condTermFlagB = 0;

  //----------A------------
  if ((mbAddrA < 0 &&
       IS_INTRA_Prediction_Mode(
           picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode) == false) ||
      (mbAddrA >= 0 && transBlockA == -1 &&
       picture.m_mbs[mbAddrA].m_name_of_mb_type != I_PCM) ||
      (IS_INTRA_Prediction_Mode(
           picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode) &&
       picture.m_mbs[picture.CurrMbAddr].constrained_intra_pred_flag == 1 &&
       mbAddrA >= 0 &&
       IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrA].m_mb_pred_mode) ==
           false &&
       (picture.m_slice->slice_header->nal_unit_type >= 2 &&
        picture.m_slice->slice_header->nal_unit_type <= 4))) {
    condTermFlagA = 0;
  } else if ((mbAddrA < 0 &&
              (IS_INTRA_Prediction_Mode(
                  picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode))) ||
             picture.m_mbs[mbAddrA].m_name_of_mb_type == I_PCM)
    condTermFlagA = 1;
  else
    condTermFlagA = transBlockA_coded_block_flag;

  //----------B------------
  if ((mbAddrB < 0 &&
       IS_INTRA_Prediction_Mode(
           picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode) == false) ||
      (mbAddrB >= 0 && transBlockB == -1 &&
       picture.m_mbs[mbAddrB].m_name_of_mb_type != I_PCM) ||
      (IS_INTRA_Prediction_Mode(
           picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode) &&
       picture.m_mbs[picture.CurrMbAddr].constrained_intra_pred_flag == 1 &&
       mbAddrB >= 0 &&
       IS_INTRA_Prediction_Mode(picture.m_mbs[mbAddrB].m_mb_pred_mode) ==
           false &&
       (picture.m_slice->slice_header->nal_unit_type >= 2 &&
        picture.m_slice->slice_header->nal_unit_type <= 4))) {
    condTermFlagB = 0;
  } else if ((mbAddrB < 0 &&
              (IS_INTRA_Prediction_Mode(
                  picture.m_mbs[picture.CurrMbAddr].m_mb_pred_mode))) ||
             picture.m_mbs[mbAddrB].m_name_of_mb_type == I_PCM)
    condTermFlagB = 1;
  else
    condTermFlagB = transBlockB_coded_block_flag;

  //------ctxIdxInc( ctxBlockCat )--------------
  ctxIdxInc = condTermFlagA + 2 * condTermFlagB;

  return 0;
}

// 9.3.3.1.1.10 Derivation process of ctxIdxInc for the syntax element transform_size_8x8_flag
int Cabac::decode_transform_size_8x8_flag(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 399;
  RET(derivation_ctxIdxInc_for_transform_size_8x8_flag(ctxIdxInc));

  ctxIdx = ctxIdxOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;
  return 0;
}

// 9.3.3.1.1.10 Derivation process of ctxIdxInc for the syntax element transform_size_8x8_flag
int Cabac::derivation_ctxIdxInc_for_transform_size_8x8_flag(
    int32_t &ctxIdxInc) {
  const bool MbaffFrameFlag = picture.m_slice->slice_header->MbaffFrameFlag;
  int32_t mbAddrA = 0, mbAddrB = 0, isChroma = 0;
  RET(picture.derivation_for_neighbouring_macroblocks(
      MbaffFrameFlag, picture.CurrMbAddr, mbAddrA, mbAddrB, isChroma));

  int32_t condTermFlagA = 1, condTermFlagB = 1;
  if (mbAddrA < 0 || picture.m_mbs[mbAddrA].transform_size_8x8_flag == 0)
    condTermFlagA = 0;
  if (mbAddrB < 0 || picture.m_mbs[mbAddrB].transform_size_8x8_flag == 0)
    condTermFlagB = 0;

  ctxIdxInc = condTermFlagA + condTermFlagB;
  return 0;
}

// 如果last_flag=1,则表示 CABAC_decode_last_significant_coeff_flag(...)
int Cabac::decode_significant_coeff_flag(MB_RESIDUAL_LEVEL mb_block_level,
                                         int32_t levelListIdx,
                                         int32_t last_flag, int32_t &synElVal) {
  int32_t NumC8x8 = 4 / (picture.m_slice->slice_header->m_sps->SubWidthC *
                         picture.m_slice->slice_header->m_sps->SubHeightC);
  int32_t mb_field_decoding_flag =
      picture.m_mbs[picture.CurrMbAddr].mb_field_decoding_flag;
  int32_t ctxIdxOffset = 0, ctxIdxInc = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;
  int32_t ctxBlockCat = 0;

  ctxBlockCat = mb_block_level;

  if (ctxBlockCat < 5) {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 105 : 277;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 166 : 338;
  } else if (ctxBlockCat == 5) {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 402 : 436;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 417 : 451;
  } else if (ctxBlockCat > 5 && ctxBlockCat < 9) {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 484 : 776;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 572 : 864;
  } else if (ctxBlockCat > 9 && ctxBlockCat < 13) {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 528 : 820;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 616 : 908;
  } else if (ctxBlockCat == 9) {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 660 : 675;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 690 : 699;
  } else {
    if (last_flag == 0)
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 718 : 733;
    else
      ctxIdxOffset = (mb_field_decoding_flag == 0) ? 748 : 757;
  }

  // Table 9-40 – Assignment of ctxIdxBlockCatOffset to ctxBlockCat for syntax elements coded_block_flag, significant_coeff_flag, last_significant_coeff_flag, and coeff_abs_level_minus1

  const int32_t ctxIdxBlockCatOffset_arr[14] = {0,  15, 29, 44, 47, 0,  0,
                                                15, 29, 0,  0,  15, 29, 0};
  int32_t ctxIdxBlockCatOffset = ctxIdxBlockCatOffset_arr[ctxBlockCat];

  // 9.3.3.1.3 Assignment process of ctxIdxInc for syntax elements significant_coeff_flag, last_significant_coeff_flag Let the variable levelListIdx be set equal to the index of the list of transform coefficient levels as specified in clause 7.4.5.3.
  if (ctxBlockCat != 3 && ctxBlockCat != 5 && ctxBlockCat != 9 &&
      ctxBlockCat != 13) {
    ctxIdxInc = levelListIdx;
  } else if (ctxBlockCat == 3) {
    RET(levelListIdx < 0 || levelListIdx > 4 * NumC8x8 - 2);
    ctxIdxInc = MIN(levelListIdx / NumC8x8, 2);
  } else if (ctxBlockCat == 5 || ctxBlockCat == 9 || ctxBlockCat == 13) {
    // Table 9-43 – Mapping of scanning position to ctxIdxInc for ctxBlockCat = = 5, 9, or 13
    const int32_t ctxIdxInc_coeff[63][3] = {
        {0, 0, 0},   {1, 1, 1},   {2, 1, 1},   {3, 2, 1},   {4, 2, 1},
        {5, 3, 1},   {5, 3, 1},   {4, 4, 1},   {4, 5, 1},   {3, 6, 1},
        {3, 7, 1},   {4, 7, 1},   {4, 7, 1},   {4, 8, 1},   {5, 4, 1},
        {5, 5, 1},   {4, 6, 2},   {4, 9, 2},   {4, 10, 2},  {4, 10, 2},
        {3, 8, 2},   {3, 11, 2},  {6, 12, 2},  {7, 11, 2},  {7, 9, 2},
        {7, 9, 2},   {8, 10, 2},  {9, 10, 2},  {10, 8, 2},  {9, 11, 2},
        {8, 12, 2},  {7, 11, 2},  {7, 9, 3},   {6, 9, 3},   {11, 10, 3},
        {12, 10, 3}, {13, 8, 3},  {11, 11, 3}, {6, 12, 3},  {7, 11, 3},
        {8, 9, 4},   {9, 9, 4},   {14, 10, 4}, {10, 10, 4}, {9, 8, 4},
        {8, 13, 4},  {6, 13, 4},  {11, 9, 4},  {12, 9, 5},  {13, 10, 5},
        {11, 10, 5}, {6, 8, 5},   {9, 13, 6},  {14, 13, 6}, {10, 9, 6},
        {9, 9, 6},   {11, 10, 7}, {12, 10, 7}, {13, 14, 7}, {11, 14, 7},
        {14, 14, 8}, {10, 14, 8}, {12, 14, 8}};

    RET(levelListIdx < 0 || levelListIdx > 63);

    if (last_flag == 0)
      ctxIdxInc = ctxIdxInc_coeff[levelListIdx][mb_field_decoding_flag];
    else
      ctxIdxInc = ctxIdxInc_coeff[levelListIdx][2];
  }

  ctxIdx = ctxIdxOffset + ctxIdxBlockCatOffset + ctxIdxInc;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;
  return 0;
}

int Cabac::decode_coeff_sign_flag(int32_t &synElVal) {
  synElVal = ff_decode_bypass();
  return 0;
}

int Cabac::decode_end_of_slice_flag(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, binVal = 0, ctxIdx = 0, bypassFlag = 0;
  ctxIdxOffset = 276;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;
  return 0;
}

int Cabac::decode_prev_intra4x4_or_intra8x8_pred_mode_flag(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 68;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;
  return 0;
}

int Cabac::decode_rem_intra4x4_or_intra8x8_pred_mode(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 69;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;

  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal += binVal << 1;

  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal += binVal << 2;

  return 0;
}

// 9.3.3.2 Arithmetic decoding process
/* 输入：在第9.3.3.1节中导出的bypassFlag、ctxIdx以及算术解码引擎的状态变量codIRange和codIOffset 
 * 输出：bin 的值*/
int Cabac::decodeBin(int32_t bypassFlag, int32_t ctxIdx, int32_t &bin) {
  int ret = 0;
  // 解码不依赖于上下文模型的状态，简单地从比特流中直接读取下一个比特
  if (bypassFlag) ret = decodeBypass(bin);
  // 根据指定的上下文索引 ctxIdx 解码二进制符号
  else
    ret = ff_decodeDecision(ctxIdx, bin);

  //std::cout << "decode_bin() -> ctxIdx=" << ctxIdx << ",bin=" << bin
  //<< std::endl;

  return ret;
}

// 9.3.3.2.3 Bypass decoding process for binary decisions
/* 输入: 切片数据的位以及变量 codIRange 和 codIOffset。  
 * 输出: 更新的变量 codIOffset 和解码值 binVal。 */
int Cabac::decodeBypass(int32_t &binVal) {
  // 1. 默认为0比特值
  binVal = 0;
  // 2. 将已经解码的位流中添加一个新的比特，在已累积的已经编码位流中进行累加
  ivlOffset = (ivlOffset << 1) | bs.readUn(1);
  // 3. 进行范围调整，以保持编码过程的精度和避免溢出
  if (ivlOffset >= ivlCurrRange) binVal = 1, ivlOffset -= ivlCurrRange;
  //RET(codIOffset >= codIRange);
  return 0;
}

int Cabac::decodeBypass() {
  // 1. 默认为0比特值
  int binVal = 0;
  // 2. 将已经解码的位流中添加一个新的比特，在已累积的已经编码位流中进行累加
  ivlOffset = (ivlOffset << 1) | bs.readUn(1);
  // 3. 进行范围调整，以保持编码过程的精度和避免溢出
  if (ivlOffset >= ivlCurrRange) binVal = 1, ivlOffset -= ivlCurrRange;
  //RET(codIOffset >= codIRange);
  return binVal;
}

// 9.3.3.2.4 Decoding process for binary decisions before termination
/* 输入: 来自切片数据的位以及变量 codIRange 和 codIOffset。  
 * 输出: 更新的变量 codIRange 和 codIOffset 以及解码值 binVal。 */
int Cabac::decodeTerminate(int32_t &binVal) {
  // 1. 默认为0比特值
  binVal = 0;
  // 2. 准备检测终止符号。对于终止符，通常会有一个特定的概率设置（此处通过减去2来调整）。
  ivlCurrRange -= 2;
  // 3-1. 不进行重整化，终止CABAC解码, codIOffset中插入的最后一位等于1。 NOTE:当解码end_of_slice_flag 时，寄存器codIOffset 中插入的最后一位被解释为rbsp_stop_one_bit。
  if (ivlOffset >= ivlCurrRange)
    binVal = 1;
  else // 执行范围重归一化
    return renormD();

  return 0;
}

// 9.3.3.2.2 Renormalization process in the arithmetic decoding engine
/* 输入: 来自切片数据的位以及变量 codIRange 和 codIOffset。  
 * 输出: 更新后的变量 codIRange 和 codIOffset。 */
int Cabac::renormD() {
  /* 如果codIRange大于或等于256，则不需要重新归一化，并且RenormD过程结束；  
   * 否则（codIRange 小于 256），进入重整化循环。在此循环中，codIRange 的值加倍，即左移 1，并使用 read_bits( 1 ) 将一位移入 codIOffset 中。  */
  while (ivlCurrRange < 256) {
    ivlCurrRange = ivlCurrRange << 1;
    ivlOffset = (ivlOffset << 1) | bs.readUn(1);
  }
  return (ivlOffset >= ivlCurrRange);
}

// 9.3.3.2.1 Arithmetic decoding process for a binary decision
/* 输入: ctxIdx、codIRange 和 codIOffset。  
 * 输出: 解码值 binVal 以及更新的变量 codIRange 和 codIOffset。*/
int Cabac::decodeDecision(int32_t ctxIdx, int32_t &binVal) {
  // 1. 取第7,8位得到查询 LPS 表的索引，这里通过量化 codIRange 来选择不同的 LPS 概率区间
  int32_t qCodIRangeIdx = (ivlCurrRange >> 6) & 0b11;
  // 2. 给定上下文 ctxIdx 相关联的状态索引 pStateIdx，决定 MPS 和 LPS 的概率模型
  int32_t pStateIdx = pStateIdxs[ctxIdx];
  // 3. 通过查表（rangeTabLPS）获得 LPS 对应的范围值 codIRangeLPS。
  int32_t codIRangeLPS = rangeTabLPS[pStateIdx][qCodIRangeIdx];

  // 4. 逐步缩小范围来逼近当前符号的概率区间
  ivlCurrRange -= codIRangeLPS;
  // 5. 取得当前上下文的 MPS（最可能符号），表明此上下文中编码符号是 0 还是 1 的概率更大。
  bool valMPS = valMPSs[ctxIdx];
  // 6. 如果当前偏移量 codIOffset 大于等于 codIRange，则说明发生了 LPS 事件（即发生了最不可能的符号）
  if (ivlOffset >= ivlCurrRange) {
    // a. 将 binVal 设置为与 valMPS 相反的值
    binVal = !valMPS;
    // b. 更新 codIOffset，以便进入下一步解码
    ivlOffset -= ivlCurrRange;
    // c. 更新 codIRange，以便解码新的符号
    ivlCurrRange = codIRangeLPS;

    // d. pStateIdx 为 0，反转当前上下文的 valMPS 值。状态索引为 0 时，表示 MPS 的概率非常低，因此有可能会发生 MPS 和 LPS 反转。
    if (pStateIdx == 0) valMPSs[ctxIdx] = !valMPS;
    // e. 根据 pStateIdx 使用 LPS 转移表 transIdxLPS 更新当前上下文的状态索引。这是 CABAC 中的自适应概率更新机制的一部分，根据当前符号的发生情况动态调整状态。
    pStateIdxs[ctxIdx] = transIdxLPS[pStateIdx];
  }
  // 7. 如果发生的是 MPS（最可能符号）
  else {
    // a. 直接将 binVal 设置为当前上下文的 MPS 值
    binVal = valMPS;
    // b. 根据 pStateIdx 使用 MPS 转移表 transIdxMPS 更新当前上下文的状态索引。
    pStateIdxs[ctxIdx] = transIdxMPS[pStateIdx];
  }
  // 8. 执行范围重归一化
  return renormD();
}

#define H264_LPS_RANGE_OFFSET 512
static const uint8_t *const ff_rangeTabLPS =
    ff_h264_cabac_tables + H264_LPS_RANGE_OFFSET;
int Cabac::ff_decodeDecision(int32_t ctxIdx, int32_t &binVal) {
  int32_t pStateIdx = preCtxState[ctxIdx];
  int32_t qCodIRangeIdx = (ivlCurrRange >> 6) & 0b11;
  int codIRangeLPS = ff_rangeTabLPS[2 * (ivlCurrRange & 0xC0) + pStateIdx];
  int bit, lps_mask;

  ivlCurrRange -= codIRangeLPS;
  lps_mask = ((ivlCurrRange << (CABAC_BITS + 1)) - ivlOffset) >> 31;

  if (-504934113 == ivlOffset) {
    int a = 0;
  }
  //printf("ivlOffset:%d\n", ivlOffset);
  ivlOffset -= (ivlCurrRange << (CABAC_BITS + 1)) & lps_mask;
  ivlCurrRange += (codIRangeLPS - ivlCurrRange) & lps_mask;

  pStateIdx ^= lps_mask;
  preCtxState[ctxIdx] = (ff_h264_mlps_state + 128)[pStateIdx];
  bit = pStateIdx & 1;

  lps_mask = ff_h264_norm_shift[ivlCurrRange];
  ivlCurrRange <<= lps_mask;
  ivlOffset <<= lps_mask;
  if (!(ivlOffset & CABAC_MASK)) refill2();
  binVal = bit;
  //printf("pStateIdx:%d\n", pStateIdx);
  //printf("codIRangeLPS:%d\n", codIRangeLPS);
  //printf("ivlOffset:%d\n", ivlOffset);
  //printf("ivlCurrRange:%d\n", ivlCurrRange);
  return 0;
}

int Cabac::initialization_decoding_engine() {
  // 该过程的输出是初始化的解码引擎寄存器 ivlCurrRange 和 ivlOffset，均为 16 位寄存器精度
  // 算术解码引擎的状态由变量ivlCurrRange和ivlOffset表示。
  ivlCurrRange = 510;
  // 该值解释为无符号整数的 9 位二进制表示形式
  ivlOffset = bs.readUn(9);
  return 0;
}

#define CABAC_BITS 16
int Cabac::ff_initialization_decoding_engine() {
  // BitStream::getEndBuf() 指向最后一个有效字节，CABAC 这里使用 one-past-end。
  return ff_init_cabac_decoder_like_ffmpeg(
      bytestream_start, bytestream, bytestream_end,
      ivlOffset, ivlCurrRange, bs.getP(), bs.getEndBuf() + 1);
}

#define H264_LPS_RANGE_OFFSET 512
#define CABAC_BITS 16
static const uint8_t *const ff_h264_lps_range =
    ff_h264_cabac_tables + H264_LPS_RANGE_OFFSET;

void Cabac::refill2() {
  int i;
  unsigned x;
  x = ivlOffset ^ (ivlOffset - 1);
  i = 7 - ff_h264_norm_shift[x >> (CABAC_BITS - 1)];

  x = -CABAC_MASK;

  const uint8_t b0 = (bytestream < bytestream_end) ? bytestream[0] : 0;
  const uint8_t b1 = (bytestream + 1 < bytestream_end) ? bytestream[1] : 0;
  x += (b0 << 9) + (b1 << 1);

  ivlOffset += x << i;
  if (bytestream < bytestream_end) bytestream += CABAC_BITS / 8;
}

// 9.3.2.2 Initialization process for context variables
int Cabac::initialization_context_variables(SliceHeader *header) {
  //In Table 9-4, the ctxIdx for which initialization is needed for each of the three initialization types, specified by the variable initType, are listed. Also listed is the table number that includes the values of initValue needed for the initialization. For P and B slice types, the derivation of initType depends on the value of the cabac_init_flag syntax element. The variable initType is derived as follows
  int initType = 0;
  if (header->slice_type == HEVC_SLICE_I)
    initType = 0;
  else if (header->slice_type == HEVC_SLICE_P)
    initType = header->cabac_init_flag ? 2 : 1;
  else
    initType = header->cabac_init_flag ? 1 : 2;

  //Initialization variable  ctxIdx of sao_merge_left_flag and sao_merge_up_flag  01 2  initValue 153 153 153
  //表9-5到表9-37包含在上下文变量的初始化中使用的8位变量initValue的值，这些变量被分配给第7.3.8.1节到第7.3.8.12节中的所有语法元素，除了end_of_slice_segment_flag、end_of_subset_one_bit和pcm_flag之外。
  for (int i = 0; i < HEVC_CONTEXTS; i++) {
    int initValue = init_values[initType][i];
    int slopeIdx = (initValue >> 4);
    int offsetIdx = initValue & 15;
    int m = slopeIdx * 5 - 45;
    int n = (offsetIdx << 3) - 16;
    int pre = CLIP3(1, 126, ((m * CLIP3(0, 51, header->SliceQpY)) >> 4) + n);
    preCtxState[i] = pre;
    valMPSs[i] = (pre <= 63) ? 0 : 1;
    /* TODO YangJing 不同初始状态的pStateIdxs只会影响CABAC的解码速度，即逼近区间的速度 <24-12-14 18:03:39> */
    pStateIdxs[i] = valMPSs[i] ? (pre - 64) : (63 - pre);
  }
  return 0;
}

int av_clip_c(int a, int amin, int amax) {
#if defined(HAVE_AV_CONFIG_H) && defined(ASSERT_LEVEL) && ASSERT_LEVEL >= 2
  if (amin > amax) abort();
#endif
  if (a < amin)
    return amin;
  else if (a > amax)
    return amax;
  else
    return a;
}

#define av_clip av_clip_c
void Cabac::ff_initialization_context_variables(SliceHeader *header) {
  int init_type = 2 - header->slice_type;
  int i;

  if (header->cabac_init_flag && header->slice_type != HEVC_SLICE_I)
    init_type ^= 3;

  for (i = 0; i < HEVC_CONTEXTS; i++) {
    int init_value = init_values[init_type][i];
    int m = (init_value >> 4) * 5 - 45;
    int n = ((init_value & 15) << 3) - 16;
    int t = ((m * av_clip(header->SliceQpY, 0, 51)) >> 4);
    int pre = 2 * (t + n) - 127;
    pre ^= pre >> 31;
    if (pre > 124) pre = 124 + (pre & 1);
    preCtxState[i] = pre;
  }
}

//9.3.2.3 Initialization process for palette predictor entries
int Cabac::initialization_palette_predictor_entries(SPS *sps, PPS *pps) {
  int numComps = (sps->chroma_format_idc == 0) ? 1 : 3;
  int PredictorPaletteSize = 0;
  int PredictorPaletteEntries[32][32] = {0};
  if (pps->pps_palette_predictor_initializers_present_flag == 1) {
    PredictorPaletteSize = pps->pps_num_palette_predictor_initializers;
    for (int comp = 0; comp < numComps; comp++)
      for (int i = 0; i < PredictorPaletteSize; i++)
        PredictorPaletteEntries[comp][i] =
            pps->pps_palette_predictor_initializer[comp][i];
  } else if (pps->pps_palette_predictor_initializers_present_flag == 0 &&
             sps->sps_palette_predictor_initializers_present_flag == 1) {
    PredictorPaletteSize =
        sps->sps_num_palette_predictor_initializers_minus1 + 1;
    for (int comp = 0; comp < numComps; comp++)
      for (int i = 0; i < PredictorPaletteSize; i++)
        PredictorPaletteEntries[comp][i] =
            sps->sps_palette_predictor_initializer[comp][i];
  } else
    PredictorPaletteSize = 0;

  return 0;
}

int Cabac::decode_sao_type_idx_luma(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;

  synElVal = 0;
  ctxIdxOffset = 1;
  ctxIdx = ctxIdxOffset + 0;
  decodeBin(0, ctxIdx, binVal);
  if (binVal == 1) {
    binVal = ff_decode_bypass();
    synElVal = 1 + binVal;
  }
  return 0;
}

int Cabac::decode_sao_offset_abs(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, binVal = 0, ctxIdx = 0, bypassFlag = 0;

  int i = 0;
  int length =
      (1 << (MIN(picture.m_slice->slice_header->m_sps->bit_depth_luma, 10) -
             5)) -
      1;

  while (i < length && ff_decode_bypass())
    i++;

  synElVal = i;

  return 0;
}

int Cabac::decode_sao_offset_sign(int32_t &synElVal) {
  synElVal = ff_decode_bypass();

  return 0;
}

int Cabac::decode_sao_band_position(int32_t &synElVal) {
  int value = ff_decode_bypass();
  for (int i = 0; i < 4; i++)
    value = (value << 1) | ff_decode_bypass();
  synElVal = value;
  return 0;
}

int Cabac::decode_sao_eo_class(int32_t &synElVal) {
  int value = ff_decode_bypass() << 1;
  value |= ff_decode_bypass();
  synElVal = value;
  return 0;
}

int Cabac::deocde_sao_merge_left_flag(int32_t &synElVal) {
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  ctxIdxOffset = 0;
  ctxIdx = ctxIdxOffset + 0;
  RET(decodeBin(bypassFlag, ctxIdx, binVal));
  synElVal = binVal;

  return 0;
}

#define av_mod_uintp2 av_mod_uintp2_c
static unsigned av_mod_uintp2_c(unsigned a, unsigned p) {
  return a & ((1U << p) - 1);
}
int Cabac::decode_split_cu_flag(int32_t &synElVal, SPS &sps,
                                uint8_t *tab_ct_depth, int ctb_left_flag,
                                int ctb_up_flag, int ct_depth, int x0, int y0) {
  int32_t ctxIdxOffset = 0, ctxIdx = 0;
  int32_t binVal = 0;
  int32_t bypassFlag = 0;

  int depth_left = 0, depth_top = 0;
  int x0b = av_mod_uintp2(x0, sps.CtbLog2SizeY);
  int y0b = av_mod_uintp2(y0, sps.CtbLog2SizeY);
  int x_cb = x0 >> sps.log2_min_luma_coding_block_size;
  int y_cb = y0 >> sps.log2_min_luma_coding_block_size;

  if (ctb_left_flag || x0b)
    depth_left = tab_ct_depth[(y_cb)*sps.min_cb_width + x_cb - 1];
  if (ctb_up_flag || y0b)
    depth_top = tab_ct_depth[(y_cb - 1) * sps.min_cb_width + x_cb];

  int inc = 0;
  inc += (depth_left > ct_depth);
  inc += (depth_top > ct_depth);
  std::cout << "elem_offset[SPLIT_CODING_UNIT_FLAG] + inc:"
            << elem_offset[SPLIT_CODING_UNIT_FLAG] + inc << std::endl;
  RET(decodeBin(0, elem_offset[SPLIT_CODING_UNIT_FLAG] + inc, binVal));
  synElVal = binVal;

  //RET(decodeBin(bypassFlag, ctxIdx, binVal));
  //synElVal += binVal << 1;

  //RET(decodeBin(bypassFlag, ctxIdx, binVal));
  //synElVal += binVal << 2;

  return 0;
}

#define SAMPLE_CTB(tab, x, y) ((tab)[x][y]])
int Cabac::decode_cu_skip_flag(int x0, int y0, int x_cb, int y_cb,
                               int ctb_left_flag, int ctb_up_flag,
                               const uint8_t *skip_flag_map,
                               int skip_flag_stride) {
  SPS *m_sps = picture.m_slice->slice_header->m_sps;
  int inc = 0;
  int x0b = av_mod_uintp2(x0, m_sps->CtbLog2SizeY);
  int y0b = av_mod_uintp2(y0, m_sps->CtbLog2SizeY);

  if (skip_flag_map && skip_flag_stride > 0) {
    if (ctb_left_flag || x0b)
      inc = !!skip_flag_map[y_cb * skip_flag_stride + (x_cb - 1)];
    if (ctb_up_flag || y0b)
      inc += !!skip_flag_map[(y_cb - 1) * skip_flag_stride + x_cb];
  }

  return decode_bin(elem_offset[SKIP_FLAG] + inc);
}

int Cabac::decode_bin(int32_t ctxIdx) {
  int bin = 0;
  ff_decodeDecision(ctxIdx, bin);
  //std::cout << "decode_bin() -> ctxIdx=" << ctxIdx << ",bin=" << bin
  //<< std::endl;
  return bin;
}

void Cabac::refill() {
  const uint8_t b0 = (bytestream < bytestream_end) ? bytestream[0] : 0;
  const uint8_t b1 = (bytestream + 1 < bytestream_end) ? bytestream[1] : 0;
  ivlOffset += (b0 << 9) + (b1 << 1);
  ivlOffset -= CABAC_MASK;
  if (bytestream < bytestream_end) bytestream += CABAC_BITS / 8;
}

int Cabac::ff_decode_bypass() {
  //printf("ivlOffset:%d\n", ivlOffset);
  int range;
  ivlOffset += ivlOffset;

  if (!(ivlOffset & CABAC_MASK)) refill();

  range = ivlCurrRange << (CABAC_BITS + 1);
  if (ivlOffset < range) {
    return 0;
  } else {
    ivlOffset -= range;
    return 1;
  }
}

int Cabac::ff_hevc_mpm_idx_decode() {
  int i = 0;
  while (i < 2 && ff_decode_bypass())
    i++;
  return i;
}

int Cabac::ff_hevc_rem_intra_luma_pred_mode_decode() {
  int i;
  int value = ff_decode_bypass();

  for (i = 0; i < 4; i++)
    value = (value << 1) | ff_decode_bypass();
  return value;
}

void Cabac::renorm_cabac_decoder_once() {
  int shift = (uint32_t)(ivlCurrRange - 0x100) >> 31;
  ivlCurrRange <<= shift;
  ivlOffset <<= shift;
  if (!(ivlOffset & CABAC_MASK)) refill();
}

int Cabac::ff_decode_terminate() {
  ivlCurrRange -= 2;
  if (ivlOffset < ivlCurrRange << (CABAC_BITS + 1)) {
    renorm_cabac_decoder_once();
    return 0;
  } else {
    return 1;
  }
}

int Cabac::ff_reinit_from_current_position() {
  const uint8_t *ptr = bytestream;
  if (ivlOffset & 0x1) ptr--;
#if CABAC_BITS == 16
  if (ivlOffset & 0x1FF) ptr--;
#endif
  if (ptr == nullptr || ptr < bytestream_start || ptr >= bytestream_end)
    return -1;

  return ff_init_cabac_decoder_like_ffmpeg(bytestream_start, bytestream,
                                           bytestream_end, ivlOffset,
                                           ivlCurrRange, ptr, bytestream_end);
}

int Cabac::ff_reinit_from(const uint8_t *buf, const uint8_t *end) {
  return ff_init_cabac_decoder_like_ffmpeg(bytestream_start, bytestream,
                                           bytestream_end, ivlOffset,
                                           ivlCurrRange, buf, end);
}

void Cabac::ff_save_context_states(uint8_t *dst) {
  if (dst == nullptr) return;
  memcpy(dst, preCtxState, HEVC_CONTEXTS);
}

void Cabac::ff_load_context_states(const uint8_t *src) {
  if (src == nullptr) return;
  memcpy(preCtxState, src, HEVC_CONTEXTS);
}

int Cabac::ff_hevc_intra_chroma_pred_mode_decode() {
  int ret;
  if (!decode_bin(elem_offset[INTRA_CHROMA_PRED_MODE])) return 4;

  ret = ff_decode_bypass() << 1;
  ret |= ff_decode_bypass();
  return ret;
}

int Cabac::ff_hevc_merge_idx_decode(int MaxNumMergeCand) {
  int i = decode_bin(elem_offset[MERGE_IDX]);

  if (i != 0) {
    while (i < MaxNumMergeCand - 1 && ff_decode_bypass())
      i++;
  }
  return i;
}

int Cabac::ff_hevc_inter_pred_idc_decode(int nPbW, int nPbH, int ct_depth) {
  if (nPbW + nPbH == 12) return decode_bin(elem_offset[INTER_PRED_IDC] + 4);
  if (decode_bin(elem_offset[INTER_PRED_IDC] + ct_depth)) return PRED_BI;

  return decode_bin(elem_offset[INTER_PRED_IDC] + 4);
}

int Cabac::ff_hevc_log2_res_scale_abs(int idx) {
  int i = 0;

  while (i < 4 && decode_bin(elem_offset[LOG2_RES_SCALE_ABS] + 4 * idx + i))
    i++;

  return i;
}

int Cabac::ff_hevc_ref_idx_lx_decode(int num_ref_idx_lx) {
  int i = 0;
  int max = num_ref_idx_lx - 1;
  int max_ctx = MIN(max, 2);

  while (i < max_ctx && decode_bin(elem_offset[REF_IDX_L0] + i))
    i++;
  if (i == 2) {
    while (i < max && ff_decode_bypass())
      i++;
  }

  return i;
}

int Cabac::ff_hevc_mvp_lx_flag_decode() {
  return decode_bin(elem_offset[MVP_LX_FLAG]);
}

int Cabac::ff_hevc_no_residual_syntax_flag_decode() {
  return decode_bin(elem_offset[NO_RESIDUAL_DATA_FLAG]);
}

int Cabac::ff_hevc_cu_chroma_qp_offset_idx(
    int chroma_qp_offset_list_len_minus1) {
  int c_max = MAX(5, chroma_qp_offset_list_len_minus1);
  int i = 0;

  while (i < c_max && decode_bin(elem_offset[CU_CHROMA_QP_OFFSET_IDX]))
    i++;

  return i;
}

int Cabac::abs_mvd_greater0_flag_decode() {
  return decode_bin(elem_offset[ABS_MVD_GREATER0_FLAG]);
}

int Cabac::abs_mvd_greater1_flag_decode() {
  return decode_bin(elem_offset[ABS_MVD_GREATER1_FLAG] + 1);
}

int Cabac::mvd_decode() {
  int ret = 2;
  int k = 1;

  while (k < CABAC_MAX_BIN && ff_decode_bypass()) {
    ret += 1U << k;
    k++;
  }
  if (k == CABAC_MAX_BIN) {
    return 0;
  }
  while (k--)
    ret += ff_decode_bypass() << k;
  return ff_decode_bypass() ? -ret : ret;
}

int Cabac::mvd_sign_flag_decode() { return ff_decode_bypass() ? -1 : 1; }

int Cabac::ff_hevc_split_transform_flag_decode(int log2_trafo_size) {
  return decode_bin(elem_offset[SPLIT_TRANSFORM_FLAG] + 5 - log2_trafo_size);
}

int Cabac::ff_hevc_cbf_cb_cr_decode(int trafo_depth) {
  return decode_bin(elem_offset[CBF_CB_CR] + trafo_depth);
}

int Cabac::ff_hevc_cbf_luma_decode(int trafo_depth) {
  return decode_bin(elem_offset[CBF_LUMA] + !trafo_depth);
}

int Cabac::hevc_transform_skip_flag_decode(int c_idx) {
  return decode_bin(elem_offset[TRANSFORM_SKIP_FLAG] + !!c_idx);
}

int Cabac::explicit_rdpcm_flag_decode(int c_idx) {
  return decode_bin(elem_offset[EXPLICIT_RDPCM_FLAG] + !!c_idx);
}

int Cabac::explicit_rdpcm_dir_flag_decode(int c_idx) {
  return decode_bin(elem_offset[EXPLICIT_RDPCM_DIR_FLAG] + !!c_idx);
}

int Cabac::ff_hevc_res_scale_sign_flag(int idx) {
  return decode_bin(elem_offset[RES_SCALE_SIGN_FLAG] + idx);
}

int Cabac::ff_hevc_cu_qp_delta_abs() {
  int prefix_val = 0;
  int suffix_val = 0;
  int inc = 0;

  while (prefix_val < 5 && decode_bin(elem_offset[CU_QP_DELTA] + inc)) {
    prefix_val++;
    inc = 1;
  }
  if (prefix_val >= 5) {
    int k = 0;
    while (k < 7 && ff_decode_bypass()) {
      suffix_val += 1 << k;
      k++;
    }
    if (k == 7) {
      return -1;
    }

    while (k--)
      suffix_val += ff_decode_bypass() << k;
  }
  return prefix_val + suffix_val;
}

void Cabac::last_significant_coeff_xy_prefix_decode(int c_idx, int log2_size,
                                                    int *last_scx_prefix,
                                                    int *last_scy_prefix) {
  int i = 0;
  int max = (log2_size << 1) - 1;
  int ctx_offset, ctx_shift;

  if (!c_idx) {
    ctx_offset = 3 * (log2_size - 2) + ((log2_size - 1) >> 2);
    ctx_shift = (log2_size + 1) >> 2;
  } else {
    ctx_offset = 15;
    ctx_shift = log2_size - 2;
  }
  while (i < max && decode_bin(elem_offset[LAST_SIGNIFICANT_COEFF_X_PREFIX] +
                               (i >> ctx_shift) + ctx_offset))
    i++;
  *last_scx_prefix = i;

  i = 0;
  while (i < max && decode_bin(elem_offset[LAST_SIGNIFICANT_COEFF_Y_PREFIX] +
                               (i >> ctx_shift) + ctx_offset))
    i++;
  *last_scy_prefix = i;
}

int Cabac::last_significant_coeff_suffix_decode(
    int last_significant_coeff_prefix) {
  int i;
  int length = (last_significant_coeff_prefix >> 1) - 1;
  int value = ff_decode_bypass();

  for (i = 1; i < length; i++)
    value = (value << 1) | ff_decode_bypass();
  return value;
}

int Cabac::significant_coeff_group_flag_decode(int c_idx, int ctx_cg) {
  int inc;

  inc = MIN(ctx_cg, 1) + (c_idx > 0 ? 2 : 0);

  return decode_bin(elem_offset[SIGNIFICANT_COEFF_GROUP_FLAG] + inc);
}

int Cabac::significant_coeff_flag_decode(int x_c, int y_c, int offset,
                                         const uint8_t *ctx_idx_map) {
  int inc = ctx_idx_map[(y_c << 2) + x_c] + offset;
  return decode_bin(elem_offset[SIGNIFICANT_COEFF_FLAG] + inc);
}

int Cabac::significant_coeff_flag_decode_0(int c_idx, int offset) {
  return decode_bin(elem_offset[SIGNIFICANT_COEFF_FLAG] + offset);
}

int Cabac::coeff_abs_level_greater1_flag_decode(int c_idx, int inc) {

  if (c_idx > 0) inc += 16;

  return decode_bin(elem_offset[COEFF_ABS_LEVEL_GREATER1_FLAG] + inc);
}

int Cabac::coeff_abs_level_greater2_flag_decode(int c_idx, int inc) {
  if (c_idx > 0) inc += 4;

  return decode_bin(elem_offset[COEFF_ABS_LEVEL_GREATER2_FLAG] + inc);
}

int Cabac::coeff_abs_level_remaining_decode(int rc_rice_param) {
  int prefix = 0;
  int suffix = 0;
  int last_coeff_abs_level_remaining;
  int i;

  while (prefix < CABAC_MAX_BIN && ff_decode_bypass())
    prefix++;

  if (prefix < 3) {
    for (i = 0; i < rc_rice_param; i++)
      suffix = (suffix << 1) | ff_decode_bypass();
    last_coeff_abs_level_remaining = (prefix << rc_rice_param) + suffix;
  } else {
    int prefix_minus3 = prefix - 3;

    if (prefix == CABAC_MAX_BIN || prefix_minus3 + rc_rice_param > 16 + 6) {
      //av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", prefix);
      return 0;
    }

    for (i = 0; i < prefix_minus3 + rc_rice_param; i++)
      suffix = (suffix << 1) | ff_decode_bypass();
    last_coeff_abs_level_remaining =
        (((1 << prefix_minus3) + 3 - 1) << rc_rice_param) + suffix;
  }
  return last_coeff_abs_level_remaining;
}

int Cabac::coeff_sign_flag_decode(uint8_t nb) {
  int i;
  int ret = 0;

  for (i = 0; i < nb; i++)
    ret = (ret << 1) | ff_decode_bypass();
  return ret;
}

int Cabac::ff_hevc_part_mode_decode(int log2_cb_size, int CuPredMode) {
  if (decode_bin(elem_offset[PART_MODE])) // 1
    return PART_2Nx2N;

  SPS *sps = picture.m_slice->slice_header->m_sps;
  if (log2_cb_size == sps->log2_min_luma_coding_block_size) {
    if (CuPredMode == MODE_INTRA) // 0
      return PART_NxN;
    if (decode_bin(elem_offset[PART_MODE] + 1)) // 01
      return PART_2NxN;
    if (log2_cb_size == 3) // 00
      return PART_Nx2N;
    if (decode_bin(elem_offset[PART_MODE] + 2)) // 001
      return PART_Nx2N;
    return PART_NxN; // 000
  }

  if (!sps->amp_enabled_flag) {
    if (decode_bin(elem_offset[PART_MODE] + 1)) // 01
      return PART_2NxN;
    return PART_Nx2N;
  }

  if (decode_bin(elem_offset[PART_MODE] + 1)) { // 01X, 01XX
    if (decode_bin(elem_offset[PART_MODE] + 3)) // 011
      return PART_2NxN;
    if (ff_decode_bypass()) // 0101
      return PART_2NxnD;
    return PART_2NxnU; // 0100
  }

  if (decode_bin(elem_offset[PART_MODE] + 3)) // 001
    return PART_Nx2N;
  if (ff_decode_bypass()) // 0001
    return PART_nRx2N;
  return PART_nLx2N; // 0000
}

#define H264_NORM_SHIFT_OFFSET 0

#define H264_LPS_RANGE_OFFSET 512

#define H264_MLPS_STATE_OFFSET 1024
