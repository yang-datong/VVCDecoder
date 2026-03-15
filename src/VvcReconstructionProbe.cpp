#include "VvcReconstructionProbe.hpp"
#include "CommonDef.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kIquantShift = 6;
constexpr int kTransformMatrixShift = 6;
constexpr int kMaxLog2TrDynamicRange = 15;

constexpr std::array<std::array<int, 6>, 2> kInvQuantScales = {{
    {{40, 45, 51, 57, 64, 72}},
    {{57, 64, 72, 80, 90, 102}},
}};

#define DEFINE_DCT2_P64_MATRIX(aa, ab, ac, ad, ae, af, ag, ah, ai, aj, ak, al, am, an, ao, ap, aq, ar, as, at, au, av, aw, ax, ay, az, ba, bb, bc, bd, be, bf, bg, bh, bi, bj, bk, bl, bm, bn, bo, bp, bq, br, bs, bt, bu, bv, bw, bx, by, bz, ca, cb, cc, cd, ce, cf, cg, ch, ci, cj, ck) \
{ \
  { aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa }, \
  { bf,  bg,  bh,  bi,  bj,  bk,  bl,  bm,  bn,  bo,  bp,  bq,  br,  bs,  bt,  bu,  bv,  bw,  bx,  by,  bz,  ca,  cb,  cc,  cd,  ce,  cf,  cg,  ch,  ci,  cj,  ck, -ck, -cj, -ci, -ch, -cg, -cf, -ce, -cd, -cc, -cb, -ca, -bz, -by, -bx, -bw, -bv, -bu, -bt, -bs, -br, -bq, -bp, -bo, -bn, -bm, -bl, -bk, -bj, -bi, -bh, -bg, -bf }, \
  { ap,  aq,  ar,  as,  at,  au,  av,  aw,  ax,  ay,  az,  ba,  bb,  bc,  bd,  be, -be, -bd, -bc, -bb, -ba, -az, -ay, -ax, -aw, -av, -au, -at, -as, -ar, -aq, -ap, -ap, -aq, -ar, -as, -at, -au, -av, -aw, -ax, -ay, -az, -ba, -bb, -bc, -bd, -be,  be,  bd,  bc,  bb,  ba,  az,  ay,  ax,  aw,  av,  au,  at,  as,  ar,  aq,  ap }, \
  { bg,  bj,  bm,  bp,  bs,  bv,  by,  cb,  ce,  ch,  ck, -ci, -cf, -cc, -bz, -bw, -bt, -bq, -bn, -bk, -bh, -bf, -bi, -bl, -bo, -br, -bu, -bx, -ca, -cd, -cg, -cj,  cj,  cg,  cd,  ca,  bx,  bu,  br,  bo,  bl,  bi,  bf,  bh,  bk,  bn,  bq,  bt,  bw,  bz,  cc,  cf,  ci, -ck, -ch, -ce, -cb, -by, -bv, -bs, -bp, -bm, -bj, -bg }, \
  { ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah,  ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah }, \
  { bh,  bm,  br,  bw,  cb,  cg, -ck, -cf, -ca, -bv, -bq, -bl, -bg, -bi, -bn, -bs, -bx, -cc, -ch,  cj,  ce,  bz,  bu,  bp,  bk,  bf,  bj,  bo,  bt,  by,  cd,  ci, -ci, -cd, -by, -bt, -bo, -bj, -bf, -bk, -bp, -bu, -bz, -ce, -cj,  ch,  cc,  bx,  bs,  bn,  bi,  bg,  bl,  bq,  bv,  ca,  cf,  ck, -cg, -cb, -bw, -br, -bm, -bh }, \
  { aq,  at,  aw,  az,  bc, -be, -bb, -ay, -av, -as, -ap, -ar, -au, -ax, -ba, -bd,  bd,  ba,  ax,  au,  ar,  ap,  as,  av,  ay,  bb,  be, -bc, -az, -aw, -at, -aq, -aq, -at, -aw, -az, -bc,  be,  bb,  ay,  av,  as,  ap,  ar,  au,  ax,  ba,  bd, -bd, -ba, -ax, -au, -ar, -ap, -as, -av, -ay, -bb, -be,  bc,  az,  aw,  at,  aq }, \
  { bi,  bp,  bw,  cd,  ck, -ce, -bx, -bq, -bj, -bh, -bo, -bv, -cc, -cj,  cf,  by,  br,  bk,  bg,  bn,  bu,  cb,  ci, -cg, -bz, -bs, -bl, -bf, -bm, -bt, -ca, -ch,  ch,  ca,  bt,  bm,  bf,  bl,  bs,  bz,  cg, -ci, -cb, -bu, -bn, -bg, -bk, -br, -by, -cf,  cj,  cc,  bv,  bo,  bh,  bj,  bq,  bx,  ce, -ck, -cd, -bw, -bp, -bi }, \
  { ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad }, \
  { bj,  bs,  cb,  ck, -cc, -bt, -bk, -bi, -br, -ca, -cj,  cd,  bu,  bl,  bh,  bq,  bz,  ci, -ce, -bv, -bm, -bg, -bp, -by, -ch,  cf,  bw,  bn,  bf,  bo,  bx,  cg, -cg, -bx, -bo, -bf, -bn, -bw, -cf,  ch,  by,  bp,  bg,  bm,  bv,  ce, -ci, -bz, -bq, -bh, -bl, -bu, -cd,  cj,  ca,  br,  bi,  bk,  bt,  cc, -ck, -cb, -bs, -bj }, \
  { ar,  aw,  bb, -bd, -ay, -at, -ap, -au, -az, -be,  ba,  av,  aq,  as,  ax,  bc, -bc, -ax, -as, -aq, -av, -ba,  be,  az,  au,  ap,  at,  ay,  bd, -bb, -aw, -ar, -ar, -aw, -bb,  bd,  ay,  at,  ap,  au,  az,  be, -ba, -av, -aq, -as, -ax, -bc,  bc,  ax,  as,  aq,  av,  ba, -be, -az, -au, -ap, -at, -ay, -bd,  bb,  aw,  ar }, \
  { bk,  bv,  cg, -ce, -bt, -bi, -bm, -bx, -ci,  cc,  br,  bg,  bo,  bz,  ck, -ca, -bp, -bf, -bq, -cb,  cj,  by,  bn,  bh,  bs,  cd, -ch, -bw, -bl, -bj, -bu, -cf,  cf,  bu,  bj,  bl,  bw,  ch, -cd, -bs, -bh, -bn, -by, -cj,  cb,  bq,  bf,  bp,  ca, -ck, -bz, -bo, -bg, -br, -cc,  ci,  bx,  bm,  bi,  bt,  ce, -cg, -bv, -bk }, \
  { ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai,  ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai }, \
  { bl,  by, -ck, -bx, -bk, -bm, -bz,  cj,  bw,  bj,  bn,  ca, -ci, -bv, -bi, -bo, -cb,  ch,  bu,  bh,  bp,  cc, -cg, -bt, -bg, -bq, -cd,  cf,  bs,  bf,  br,  ce, -ce, -br, -bf, -bs, -cf,  cd,  bq,  bg,  bt,  cg, -cc, -bp, -bh, -bu, -ch,  cb,  bo,  bi,  bv,  ci, -ca, -bn, -bj, -bw, -cj,  bz,  bm,  bk,  bx,  ck, -by, -bl }, \
  { as,  az, -bd, -aw, -ap, -av, -bc,  ba,  at,  ar,  ay, -be, -ax, -aq, -au, -bb,  bb,  au,  aq,  ax,  be, -ay, -ar, -at, -ba,  bc,  av,  ap,  aw,  bd, -az, -as, -as, -az,  bd,  aw,  ap,  av,  bc, -ba, -at, -ar, -ay,  be,  ax,  aq,  au,  bb, -bb, -au, -aq, -ax, -be,  ay,  ar,  at,  ba, -bc, -av, -ap, -aw, -bd,  az,  as }, \
  { bm,  cb, -cf, -bq, -bi, -bx,  cj,  bu,  bf,  bt,  ci, -by, -bj, -bp, -ce,  cc,  bn,  bl,  ca, -cg, -br, -bh, -bw,  ck,  bv,  bg,  bs,  ch, -bz, -bk, -bo, -cd,  cd,  bo,  bk,  bz, -ch, -bs, -bg, -bv, -ck,  bw,  bh,  br,  cg, -ca, -bl, -bn, -cc,  ce,  bp,  bj,  by, -ci, -bt, -bf, -bu, -cj,  bx,  bi,  bq,  cf, -cb, -bm }, \
  { ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab }, \
  { bn,  ce, -ca, -bj, -br, -ci,  bw,  bf,  bv, -cj, -bs, -bi, -bz,  cf,  bo,  bm,  cd, -cb, -bk, -bq, -ch,  bx,  bg,  bu, -ck, -bt, -bh, -by,  cg,  bp,  bl,  cc, -cc, -bl, -bp, -cg,  by,  bh,  bt,  ck, -bu, -bg, -bx,  ch,  bq,  bk,  cb, -cd, -bm, -bo, -cf,  bz,  bi,  bs,  cj, -bv, -bf, -bw,  ci,  br,  bj,  ca, -ce, -bn }, \
  { at,  bc, -ay, -ap, -ax,  bd,  au,  as,  bb, -az, -aq, -aw,  be,  av,  ar,  ba, -ba, -ar, -av, -be,  aw,  aq,  az, -bb, -as, -au, -bd,  ax,  ap,  ay, -bc, -at, -at, -bc,  ay,  ap,  ax, -bd, -au, -as, -bb,  az,  aq,  aw, -be, -av, -ar, -ba,  ba,  ar,  av,  be, -aw, -aq, -az,  bb,  as,  au,  bd, -ax, -ap, -ay,  bc,  at }, \
  { bo,  ch, -bv, -bh, -ca,  cc,  bj,  bt, -cj, -bq, -bm, -cf,  bx,  bf,  by, -ce, -bl, -br, -ck,  bs,  bk,  cd, -bz, -bg, -bw,  cg,  bn,  bp,  ci, -bu, -bi, -cb,  cb,  bi,  bu, -ci, -bp, -bn, -cg,  bw,  bg,  bz, -cd, -bk, -bs,  ck,  br,  bl,  ce, -by, -bf, -bx,  cf,  bm,  bq,  cj, -bt, -bj, -cc,  ca,  bh,  bv, -ch, -bo }, \
  { aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj,  aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj }, \
  { bp,  ck, -bq, -bo, -cj,  br,  bn,  ci, -bs, -bm, -ch,  bt,  bl,  cg, -bu, -bk, -cf,  bv,  bj,  ce, -bw, -bi, -cd,  bx,  bh,  cc, -by, -bg, -cb,  bz,  bf,  ca, -ca, -bf, -bz,  cb,  bg,  by, -cc, -bh, -bx,  cd,  bi,  bw, -ce, -bj, -bv,  cf,  bk,  bu, -cg, -bl, -bt,  ch,  bm,  bs, -ci, -bn, -br,  cj,  bo,  bq, -ck, -bp }, \
  { au, -be, -at, -av,  bd,  as,  aw, -bc, -ar, -ax,  bb,  aq,  ay, -ba, -ap, -az,  az,  ap,  ba, -ay, -aq, -bb,  ax,  ar,  bc, -aw, -as, -bd,  av,  at,  be, -au, -au,  be,  at,  av, -bd, -as, -aw,  bc,  ar,  ax, -bb, -aq, -ay,  ba,  ap,  az, -az, -ap, -ba,  ay,  aq,  bb, -ax, -ar, -bc,  aw,  as,  bd, -av, -at, -be,  au }, \
  { bq, -ci, -bl, -bv,  cd,  bg,  ca, -by, -bi, -cf,  bt,  bn,  ck, -bo, -bs,  cg,  bj,  bx, -cb, -bf, -cc,  bw,  bk,  ch, -br, -bp,  cj,  bm,  bu, -ce, -bh, -bz,  bz,  bh,  ce, -bu, -bm, -cj,  bp,  br, -ch, -bk, -bw,  cc,  bf,  cb, -bx, -bj, -cg,  bs,  bo, -ck, -bn, -bt,  cf,  bi,  by, -ca, -bg, -cd,  bv,  bl,  ci, -bq }, \
  { ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae }, \
  { br, -cf, -bg, -cc,  bu,  bo, -ci, -bj, -bz,  bx,  bl,  ck, -bm, -bw,  ca,  bi,  ch, -bp, -bt,  cd,  bf,  ce, -bs, -bq,  cg,  bh,  cb, -bv, -bn,  cj,  bk,  by, -by, -bk, -cj,  bn,  bv, -cb, -bh, -cg,  bq,  bs, -ce, -bf, -cd,  bt,  bp, -ch, -bi, -ca,  bw,  bm, -ck, -bl, -bx,  bz,  bj,  ci, -bo, -bu,  cc,  bg,  cf, -br }, \
  { av, -bb, -ap, -bc,  au,  aw, -ba, -aq, -bd,  at,  ax, -az, -ar, -be,  as,  ay, -ay, -as,  be,  ar,  az, -ax, -at,  bd,  aq,  ba, -aw, -au,  bc,  ap,  bb, -av, -av,  bb,  ap,  bc, -au, -aw,  ba,  aq,  bd, -at, -ax,  az,  ar,  be, -as, -ay,  ay,  as, -be, -ar, -az,  ax,  at, -bd, -aq, -ba,  aw,  au, -bc, -ap, -bb,  av }, \
  { bs, -cc, -bi, -cj,  bl,  bz, -bv, -bp,  cf,  bf,  cg, -bo, -bw,  by,  bm, -ci, -bh, -cd,  br,  bt, -cb, -bj, -ck,  bk,  ca, -bu, -bq,  ce,  bg,  ch, -bn, -bx,  bx,  bn, -ch, -bg, -ce,  bq,  bu, -ca, -bk,  ck,  bj,  cb, -bt, -br,  cd,  bh,  ci, -bm, -by,  bw,  bo, -cg, -bf, -cf,  bp,  bv, -bz, -bl,  cj,  bi,  cc, -bs }, \
  { ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak,  ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak }, \
  { bt, -bz, -bn,  cf,  bh,  ck, -bi, -ce,  bo,  by, -bu, -bs,  ca,  bm, -cg, -bg, -cj,  bj,  cd, -bp, -bx,  bv,  br, -cb, -bl,  ch,  bf,  ci, -bk, -cc,  bq,  bw, -bw, -bq,  cc,  bk, -ci, -bf, -ch,  bl,  cb, -br, -bv,  bx,  bp, -cd, -bj,  cj,  bg,  cg, -bm, -ca,  bs,  bu, -by, -bo,  ce,  bi, -ck, -bh, -cf,  bn,  bz, -bt }, \
  { aw, -ay, -au,  ba,  as, -bc, -aq,  be,  ap,  bd, -ar, -bb,  at,  az, -av, -ax,  ax,  av, -az, -at,  bb,  ar, -bd, -ap, -be,  aq,  bc, -as, -ba,  au,  ay, -aw, -aw,  ay,  au, -ba, -as,  bc,  aq, -be, -ap, -bd,  ar,  bb, -at, -az,  av,  ax, -ax, -av,  az,  at, -bb, -ar,  bd,  ap,  be, -aq, -bc,  as,  ba, -au, -ay,  aw }, \
  { bu, -bw, -bs,  by,  bq, -ca, -bo,  cc,  bm, -ce, -bk,  cg,  bi, -ci, -bg,  ck,  bf,  cj, -bh, -ch,  bj,  cf, -bl, -cd,  bn,  cb, -bp, -bz,  br,  bx, -bt, -bv,  bv,  bt, -bx, -br,  bz,  bp, -cb, -bn,  cd,  bl, -cf, -bj,  ch,  bh, -cj, -bf, -ck,  bg,  ci, -bi, -cg,  bk,  ce, -bm, -cc,  bo,  ca, -bq, -by,  bs,  bw, -bu }, \
  { aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa }, \
  { bv, -bt, -bx,  br,  bz, -bp, -cb,  bn,  cd, -bl, -cf,  bj,  ch, -bh, -cj,  bf, -ck, -bg,  ci,  bi, -cg, -bk,  ce,  bm, -cc, -bo,  ca,  bq, -by, -bs,  bw,  bu, -bu, -bw,  bs,  by, -bq, -ca,  bo,  cc, -bm, -ce,  bk,  cg, -bi, -ci,  bg,  ck, -bf,  cj,  bh, -ch, -bj,  cf,  bl, -cd, -bn,  cb,  bp, -bz, -br,  bx,  bt, -bv }, \
  { ax, -av, -az,  at,  bb, -ar, -bd,  ap, -be, -aq,  bc,  as, -ba, -au,  ay,  aw, -aw, -ay,  au,  ba, -as, -bc,  aq,  be, -ap,  bd,  ar, -bb, -at,  az,  av, -ax, -ax,  av,  az, -at, -bb,  ar,  bd, -ap,  be,  aq, -bc, -as,  ba,  au, -ay, -aw,  aw,  ay, -au, -ba,  as,  bc, -aq, -be,  ap, -bd, -ar,  bb,  at, -az, -av,  ax }, \
  { bw, -bq, -cc,  bk,  ci, -bf,  ch,  bl, -cb, -br,  bv,  bx, -bp, -cd,  bj,  cj, -bg,  cg,  bm, -ca, -bs,  bu,  by, -bo, -ce,  bi,  ck, -bh,  cf,  bn, -bz, -bt,  bt,  bz, -bn, -cf,  bh, -ck, -bi,  ce,  bo, -by, -bu,  bs,  ca, -bm, -cg,  bg, -cj, -bj,  cd,  bp, -bx, -bv,  br,  cb, -bl, -ch,  bf, -ci, -bk,  cc,  bq, -bw }, \
  { al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al,  al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al }, \
  { bx, -bn, -ch,  bg, -ce, -bq,  bu,  ca, -bk, -ck,  bj, -cb, -bt,  br,  cd, -bh,  ci,  bm, -by, -bw,  bo,  cg, -bf,  cf,  bp, -bv, -bz,  bl,  cj, -bi,  cc,  bs, -bs, -cc,  bi, -cj, -bl,  bz,  bv, -bp, -cf,  bf, -cg, -bo,  bw,  by, -bm, -ci,  bh, -cd, -br,  bt,  cb, -bj,  ck,  bk, -ca, -bu,  bq,  ce, -bg,  ch,  bn, -bx }, \
  { ay, -as, -be,  ar, -az, -ax,  at,  bd, -aq,  ba,  aw, -au, -bc,  ap, -bb, -av,  av,  bb, -ap,  bc,  au, -aw, -ba,  aq, -bd, -at,  ax,  az, -ar,  be,  as, -ay, -ay,  as,  be, -ar,  az,  ax, -at, -bd,  aq, -ba, -aw,  au,  bc, -ap,  bb,  av, -av, -bb,  ap, -bc, -au,  aw,  ba, -aq,  bd,  at, -ax, -az,  ar, -be, -as,  ay }, \
  { by, -bk,  cj,  bn, -bv, -cb,  bh, -cg, -bq,  bs,  ce, -bf,  cd,  bt, -bp, -ch,  bi, -ca, -bw,  bm,  ck, -bl,  bx,  bz, -bj,  ci,  bo, -bu, -cc,  bg, -cf, -br,  br,  cf, -bg,  cc,  bu, -bo, -ci,  bj, -bz, -bx,  bl, -ck, -bm,  bw,  ca, -bi,  ch,  bp, -bt, -cd,  bf, -ce, -bs,  bq,  cg, -bh,  cb,  bv, -bn, -cj,  bk, -by }, \
  { af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af }, \
  { bz, -bh,  ce,  bu, -bm,  cj,  bp, -br, -ch,  bk, -bw, -cc,  bf, -cb, -bx,  bj, -cg, -bs,  bo,  ck, -bn,  bt,  cf, -bi,  by,  ca, -bg,  cd,  bv, -bl,  ci,  bq, -bq, -ci,  bl, -bv, -cd,  bg, -ca, -by,  bi, -cf, -bt,  bn, -ck, -bo,  bs,  cg, -bj,  bx,  cb, -bf,  cc,  bw, -bk,  ch,  br, -bp, -cj,  bm, -bu, -ce,  bh, -bz }, \
  { az, -ap,  ba,  ay, -aq,  bb,  ax, -ar,  bc,  aw, -as,  bd,  av, -at,  be,  au, -au, -be,  at, -av, -bd,  as, -aw, -bc,  ar, -ax, -bb,  aq, -ay, -ba,  ap, -az, -az,  ap, -ba, -ay,  aq, -bb, -ax,  ar, -bc, -aw,  as, -bd, -av,  at, -be, -au,  au,  be, -at,  av,  bd, -as,  aw,  bc, -ar,  ax,  bb, -aq,  ay,  ba, -ap,  az }, \
  { ca, -bf,  bz,  cb, -bg,  by,  cc, -bh,  bx,  cd, -bi,  bw,  ce, -bj,  bv,  cf, -bk,  bu,  cg, -bl,  bt,  ch, -bm,  bs,  ci, -bn,  br,  cj, -bo,  bq,  ck, -bp,  bp, -ck, -bq,  bo, -cj, -br,  bn, -ci, -bs,  bm, -ch, -bt,  bl, -cg, -bu,  bk, -cf, -bv,  bj, -ce, -bw,  bi, -cd, -bx,  bh, -cc, -by,  bg, -cb, -bz,  bf, -ca }, \
  { am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am,  am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am }, \
  { cb, -bi,  bu,  ci, -bp,  bn, -cg, -bw,  bg, -bz, -cd,  bk, -bs, -ck,  br, -bl,  ce,  by, -bf,  bx,  cf, -bm,  bq, -cj, -bt,  bj, -cc, -ca,  bh, -bv, -ch,  bo, -bo,  ch,  bv, -bh,  ca,  cc, -bj,  bt,  cj, -bq,  bm, -cf, -bx,  bf, -by, -ce,  bl, -br,  ck,  bs, -bk,  cd,  bz, -bg,  bw,  cg, -bn,  bp, -ci, -bu,  bi, -cb }, \
  { ba, -ar,  av, -be, -aw,  aq, -az, -bb,  as, -au,  bd,  ax, -ap,  ay,  bc, -at,  at, -bc, -ay,  ap, -ax, -bd,  au, -as,  bb,  az, -aq,  aw,  be, -av,  ar, -ba, -ba,  ar, -av,  be,  aw, -aq,  az,  bb, -as,  au, -bd, -ax,  ap, -ay, -bc,  at, -at,  bc,  ay, -ap,  ax,  bd, -au,  as, -bb, -az,  aq, -aw, -be,  av, -ar,  ba }, \
  { cc, -bl,  bp, -cg, -by,  bh, -bt,  ck,  bu, -bg,  bx,  ch, -bq,  bk, -cb, -cd,  bm, -bo,  cf,  bz, -bi,  bs, -cj, -bv,  bf, -bw, -ci,  br, -bj,  ca,  ce, -bn,  bn, -ce, -ca,  bj, -br,  ci,  bw, -bf,  bv,  cj, -bs,  bi, -bz, -cf,  bo, -bm,  cd,  cb, -bk,  bq, -ch, -bx,  bg, -bu, -ck,  bt, -bh,  by,  cg, -bp,  bl, -cc }, \
  { ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac }, \
  { cd, -bo,  bk, -bz, -ch,  bs, -bg,  bv, -ck, -bw,  bh, -br,  cg,  ca, -bl,  bn, -cc, -ce,  bp, -bj,  by,  ci, -bt,  bf, -bu,  cj,  bx, -bi,  bq, -cf, -cb,  bm, -bm,  cb,  cf, -bq,  bi, -bx, -cj,  bu, -bf,  bt, -ci, -by,  bj, -bp,  ce,  cc, -bn,  bl, -ca, -cg,  br, -bh,  bw,  ck, -bv,  bg, -bs,  ch,  bz, -bk,  bo, -cd }, \
  { bb, -au,  aq, -ax,  be,  ay, -ar,  at, -ba, -bc,  av, -ap,  aw, -bd, -az,  as, -as,  az,  bd, -aw,  ap, -av,  bc,  ba, -at,  ar, -ay, -be,  ax, -aq,  au, -bb, -bb,  au, -aq,  ax, -be, -ay,  ar, -at,  ba,  bc, -av,  ap, -aw,  bd,  az, -as,  as, -az, -bd,  aw, -ap,  av, -bc, -ba,  at, -ar,  ay,  be, -ax,  aq, -au,  bb }, \
  { ce, -br,  bf, -bs,  cf,  cd, -bq,  bg, -bt,  cg,  cc, -bp,  bh, -bu,  ch,  cb, -bo,  bi, -bv,  ci,  ca, -bn,  bj, -bw,  cj,  bz, -bm,  bk, -bx,  ck,  by, -bl,  bl, -by, -ck,  bx, -bk,  bm, -bz, -cj,  bw, -bj,  bn, -ca, -ci,  bv, -bi,  bo, -cb, -ch,  bu, -bh,  bp, -cc, -cg,  bt, -bg,  bq, -cd, -cf,  bs, -bf,  br, -ce }, \
  { an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an,  an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an }, \
  { cf, -bu,  bj, -bl,  bw, -ch, -cd,  bs, -bh,  bn, -by,  cj,  cb, -bq,  bf, -bp,  ca,  ck, -bz,  bo, -bg,  br, -cc, -ci,  bx, -bm,  bi, -bt,  ce,  cg, -bv,  bk, -bk,  bv, -cg, -ce,  bt, -bi,  bm, -bx,  ci,  cc, -br,  bg, -bo,  bz, -ck, -ca,  bp, -bf,  bq, -cb, -cj,  by, -bn,  bh, -bs,  cd,  ch, -bw,  bl, -bj,  bu, -cf }, \
  { bc, -ax,  as, -aq,  av, -ba, -be,  az, -au,  ap, -at,  ay, -bd, -bb,  aw, -ar,  ar, -aw,  bb,  bd, -ay,  at, -ap,  au, -az,  be,  ba, -av,  aq, -as,  ax, -bc, -bc,  ax, -as,  aq, -av,  ba,  be, -az,  au, -ap,  at, -ay,  bd,  bb, -aw,  ar, -ar,  aw, -bb, -bd,  ay, -at,  ap, -au,  az, -be, -ba,  av, -aq,  as, -ax,  bc }, \
  { cg, -bx,  bo, -bf,  bn, -bw,  cf,  ch, -by,  bp, -bg,  bm, -bv,  ce,  ci, -bz,  bq, -bh,  bl, -bu,  cd,  cj, -ca,  br, -bi,  bk, -bt,  cc,  ck, -cb,  bs, -bj,  bj, -bs,  cb, -ck, -cc,  bt, -bk,  bi, -br,  ca, -cj, -cd,  bu, -bl,  bh, -bq,  bz, -ci, -ce,  bv, -bm,  bg, -bp,  by, -ch, -cf,  bw, -bn,  bf, -bo,  bx, -cg }, \
  { ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag }, \
  { ch, -ca,  bt, -bm,  bf, -bl,  bs, -bz,  cg,  ci, -cb,  bu, -bn,  bg, -bk,  br, -by,  cf,  cj, -cc,  bv, -bo,  bh, -bj,  bq, -bx,  ce,  ck, -cd,  bw, -bp,  bi, -bi,  bp, -bw,  cd, -ck, -ce,  bx, -bq,  bj, -bh,  bo, -bv,  cc, -cj, -cf,  by, -br,  bk, -bg,  bn, -bu,  cb, -ci, -cg,  bz, -bs,  bl, -bf,  bm, -bt,  ca, -ch }, \
  { bd, -ba,  ax, -au,  ar, -ap,  as, -av,  ay, -bb,  be,  bc, -az,  aw, -at,  aq, -aq,  at, -aw,  az, -bc, -be,  bb, -ay,  av, -as,  ap, -ar,  au, -ax,  ba, -bd, -bd,  ba, -ax,  au, -ar,  ap, -as,  av, -ay,  bb, -be, -bc,  az, -aw,  at, -aq,  aq, -at,  aw, -az,  bc,  be, -bb,  ay, -av,  as, -ap,  ar, -au,  ax, -ba,  bd }, \
  { ci, -cd,  by, -bt,  bo, -bj,  bf, -bk,  bp, -bu,  bz, -ce,  cj,  ch, -cc,  bx, -bs,  bn, -bi,  bg, -bl,  bq, -bv,  ca, -cf,  ck,  cg, -cb,  bw, -br,  bm, -bh,  bh, -bm,  br, -bw,  cb, -cg, -ck,  cf, -ca,  bv, -bq,  bl, -bg,  bi, -bn,  bs, -bx,  cc, -ch, -cj,  ce, -bz,  bu, -bp,  bk, -bf,  bj, -bo,  bt, -by,  cd, -ci }, \
  { ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao,  ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao }, \
  { cj, -cg,  cd, -ca,  bx, -bu,  br, -bo,  bl, -bi,  bf, -bh,  bk, -bn,  bq, -bt,  bw, -bz,  cc, -cf,  ci,  ck, -ch,  ce, -cb,  by, -bv,  bs, -bp,  bm, -bj,  bg, -bg,  bj, -bm,  bp, -bs,  bv, -by,  cb, -ce,  ch, -ck, -ci,  cf, -cc,  bz, -bw,  bt, -bq,  bn, -bk,  bh, -bf,  bi, -bl,  bo, -br,  bu, -bx,  ca, -cd,  cg, -cj }, \
  { be, -bd,  bc, -bb,  ba, -az,  ay, -ax,  aw, -av,  au, -at,  as, -ar,  aq, -ap,  ap, -aq,  ar, -as,  at, -au,  av, -aw,  ax, -ay,  az, -ba,  bb, -bc,  bd, -be, -be,  bd, -bc,  bb, -ba,  az, -ay,  ax, -aw,  av, -au,  at, -as,  ar, -aq,  ap, -ap,  aq, -ar,  as, -at,  au, -av,  aw, -ax,  ay, -az,  ba, -bb,  bc, -bd,  be }, \
  { ck, -cj,  ci, -ch,  cg, -cf,  ce, -cd,  cc, -cb,  ca, -bz,  by, -bx,  bw, -bv,  bu, -bt,  bs, -br,  bq, -bp,  bo, -bn,  bm, -bl,  bk, -bj,  bi, -bh,  bg, -bf,  bf, -bg,  bh, -bi,  bj, -bk,  bl, -bm,  bn, -bo,  bp, -bq,  br, -bs,  bt, -bu,  bv, -bw,  bx, -by,  bz, -ca,  cb, -cc,  cd, -ce,  cf, -cg,  ch, -ci,  cj, -ck }, \
}

static const int16_t kDct2Matrix64[64][64] =
    DEFINE_DCT2_P64_MATRIX(64, 83, 36, 89, 75, 50, 18, 90, 87, 80, 70, 57, 43,
                           25, 9, 90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46,
                           38, 31, 22, 13, 4, 91, 90, 90, 90, 88, 87, 86, 84,
                           83, 81, 79, 77, 73, 71, 69, 65, 62, 59, 56, 52, 48,
                           44, 41, 37, 33, 28, 24, 20, 15, 11, 7, 2);

#undef DEFINE_DCT2_P64_MATRIX

int floorLog2(int value) {
  int log2 = -1;
  while (value > 0) {
    value >>= 1;
    ++log2;
  }
  return log2;
}

int clipTransformCoefficient(int value) {
  return Clip3(-(1 << kMaxLog2TrDynamicRange),
               (1 << kMaxLog2TrDynamicRange) - 1, value);
}

int computeBitDepth(const VvcSpsState &sps) {
  return sps.bit_depth > 0 ? sps.bit_depth : 8 + (sps.qp_bd_offset / 6);
}

void inverseTransformDct264x64(const std::vector<int> &coeffs, int bit_depth,
                               std::vector<int> &residual) {
  const int width = 64;
  const int height = 64;
  const int clip_min = -(1 << kMaxLog2TrDynamicRange);
  const int clip_max = (1 << kMaxLog2TrDynamicRange) - 1;
  const int shift_1st = kTransformMatrixShift + 1;
  const int shift_2nd =
      (kTransformMatrixShift + kMaxLog2TrDynamicRange - 1) - bit_depth;
  const int round_1st = 1 << (shift_1st - 1);
  const int round_2nd = 1 << (shift_2nd - 1);

  std::vector<int> tmp(width * height, 0);
  residual.assign(width * height, 0);

  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      int64_t sum = 0;
      for (int k = 0; k < height; ++k) {
        sum += static_cast<int64_t>(coeffs[static_cast<size_t>(k * width + x)]) *
               kDct2Matrix64[k][y];
      }
      tmp[static_cast<size_t>(y * width + x)] = Clip3(
          clip_min, clip_max, static_cast<int>((sum + round_1st) >> shift_1st));
    }
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int64_t sum = 0;
      for (int k = 0; k < width; ++k) {
        sum += static_cast<int64_t>(tmp[static_cast<size_t>(y * width + k)]) *
               kDct2Matrix64[k][x];
      }
      residual[static_cast<size_t>(y * width + x)] = Clip3(
          clip_min, clip_max, static_cast<int>((sum + round_2nd) >> shift_2nd));
    }
  }
}

} // namespace

int VvcReconstructionProbe::reconstructFirstLumaTransformBlock(
    const VvcSpsState &sps, int slice_qp_y, bool dep_quant_used,
    const VvcIntraCuProbeResult &intra_result,
    const VvcTransformTreeProbeResult &transform_result,
    const VvcResidualProbeResult &residual_result,
    VvcReconstructionProbeResult &result) {
  m_last_error.clear();
  result = VvcReconstructionProbeResult{};

  if (!sps.valid || !intra_result.valid || !transform_result.valid ||
      !residual_result.valid) {
    setError("VVC reconstruction probe requires valid syntax probe results");
    return -1;
  }
  if (transform_result.first_tu_width != 64 ||
      transform_result.first_tu_height != 64) {
    setError("Reconstruction probe currently only supports first 64x64 luma TU");
    return -1;
  }
  if (transform_result.first_tu_x0 != 0 || transform_result.first_tu_y0 != 0) {
    setError("Reconstruction probe currently only supports top-left TU");
    return -1;
  }
  if (transform_result.first_tu_ts_flag != 0) {
    setError("Reconstruction probe does not support transform-skip TU yet");
    return -1;
  }
  if (sps.scaling_list_enabled_flag) {
    setError("Reconstruction probe currently assumes scaling_list_enabled_flag=0");
    return -1;
  }
  if (intra_result.mip_flag || intra_result.multi_ref_idx != 0 ||
      intra_result.isp_mode != 0 || intra_result.intra_luma_mode != 1) {
    setError("Reconstruction probe currently only supports plain DC luma intra CU");
    return -1;
  }
  if (residual_result.coeffs.size() != static_cast<size_t>(64 * 64)) {
    setError("Residual probe coefficient buffer size does not match first TU");
    return -1;
  }
  if (sps.ctu_size <= 32 && sps.lfnst_enabled_flag) {
    setError("Reconstruction probe has not handled signaled LFNST syntax yet");
    return -1;
  }

  const int bit_depth = computeBitDepth(sps);
  const int qp = slice_qp_y + sps.qp_bd_offset;
  const int log2_width = floorLog2(transform_result.first_tu_width);
  const int log2_height = floorLog2(transform_result.first_tu_height);
  const bool need_sqrt_adjustment = ((log2_width + log2_height) & 1) != 0;
  const int original_transform_shift =
      kMaxLog2TrDynamicRange - bit_depth - ((log2_width + log2_height) >> 1);
  const int i_transform_shift =
      original_transform_shift + (need_sqrt_adjustment ? -1 : 0);
  const int qp_per =
      dep_quant_used ? ((qp + 1) / 6) : (qp / 6);
  const int qp_rem =
      dep_quant_used ? (qp + 1 - 6 * qp_per) : (qp - 6 * qp_per);
  const int right_shift =
      kIquantShift + (dep_quant_used ? 1 : 0) - i_transform_shift - qp_per;
  const int scale =
      kInvQuantScales[need_sqrt_adjustment ? 1 : 0][qp_rem];
  const int input_min = -(1 << kMaxLog2TrDynamicRange);
  const int input_max = (1 << kMaxLog2TrDynamicRange) - 1;

  std::vector<int> dequant_coeffs(residual_result.coeffs.size(), 0);
  for (size_t i = 0; i < residual_result.coeffs.size(); ++i) {
    const int level =
        Clip3(input_min, input_max, residual_result.coeffs[i]);
    if (level == 0) continue;

    int64_t value = static_cast<int64_t>(level) * scale;
    if (right_shift > 0) {
      value = (value + (int64_t(1) << (right_shift - 1))) >> right_shift;
    } else if (right_shift < 0) {
      value <<= -right_shift;
    }
    dequant_coeffs[i] = clipTransformCoefficient(static_cast<int>(value));
  }

  inverseTransformDct264x64(dequant_coeffs, bit_depth, result.residual);

  result.bit_depth = bit_depth;
  result.qp = qp;
  result.pred_dc = 1 << (bit_depth - 1);
  result.recon.assign(result.residual.size(), 0);

  result.residual_min = result.residual.empty() ? 0 : result.residual[0];
  result.residual_max = result.residual.empty() ? 0 : result.residual[0];
  result.recon_min = (1 << bit_depth) - 1;
  result.recon_max = 0;

  for (size_t i = 0; i < result.residual.size(); ++i) {
    const int resi = result.residual[i];
    result.residual_min = std::min(result.residual_min, resi);
    result.residual_max = std::max(result.residual_max, resi);
    result.residual_sum += resi;

    const int recon = ClipBD(result.pred_dc + resi, bit_depth);
    result.recon[i] = recon;
    result.recon_min = std::min(result.recon_min, recon);
    result.recon_max = std::max(result.recon_max, recon);
    result.recon_sum += recon;
  }

  result.recon_top_left = result.recon[0];
  result.recon_center = result.recon[static_cast<size_t>(32 * 64 + 32)];
  result.recon_bottom_right = result.recon.back();
  result.valid = true;
  return 0;
}
