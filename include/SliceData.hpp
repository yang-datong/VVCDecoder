#ifndef SLICEBODY_HPP_OVHTPIZQ
#define SLICEBODY_HPP_OVHTPIZQ
#include "BitStream.hpp"
#include "CU.hpp"
#include "Cabac.hpp"
#include "PU.hpp"
#include "SliceHeader.hpp"
#include "TU.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

class PictureBase;
class SliceData {
 private:
  //允许Slice类访问
  friend class Slice;
  SliceData(){};
  ~SliceData() {
    header = nullptr;
    cabac = nullptr;
    bs = nullptr;
    pic = nullptr;
    m_sps = nullptr;
    m_pps = nullptr;
  }

  PU m_pu;
  TU m_tu;
  CU m_cu;

  int coding_tree_unit();
  int sao(int32_t rx, int32_t ry);
  int coding_quadtree(int x0, int y0, int log2CbSize, int cqtDepth);
  int coding_unit(int x0, int y0, int log2CbSize);
  int prediction_unit(int x0, int y0, int nPbW, int nPbH);
  int pcm_sample(int x0, int y0, int log2CbSize);
  int transform_tree(int x0, int y0, int xBase, int yBase, int cbXBase,
                     int cbYBase, int log2CbSize, int log2TrafoSize,
                     int trafoDepth, int blkIdx, const int *base_cbf_cb,
                     const int *base_cbf_cr);
  int mvd_coding(int x0, int y0, int refList);
  int transform_unit(int x0, int y0, int xBase, int yBase, int cbXBase,
                     int cbYBase, int log2CbSize, int log2TrafoSize,
                     int trafoDepth, int blkIdx, int cbf_luma,
                     const int *cbf_cb, const int *cbf_cr);
  int reconstruct_intra_luma_block(int x0, int y0, int nCbS,
                                   int intra_mode = DC_IDX);
  //int residual_coding(int x0, int y0, int log2TrafoSize, int cIdx);
  int residual_coding(int x0, int y0, int log2TrafoSize,
                      Cabac::ScanType scan_idx, int cIdx);
  int cross_comp_pred(int x0, int y0, int c);
  int palette_coding(int x0, int y0, int nCbS);
  int delta_qp();
  int chroma_qp_offset();
  int CtbAddrInTs = 0;
  int CtbAddrInRs = 0;

  std::vector<int> tab_slice_address;
  int first_qp_group = 0;
  int end_of_tiles_x = 0;
  int end_of_tiles_y = 0;
  std::vector<uint8_t> tab_ipm;
  void hls_decode_neighbour(int x_ctb, int y_ctb, int ctb_addr_ts);

  int cabac_init(int ctb_addr_ts);

  int ff_init_cabac_decoder();
  int cabac_init_state();
  int load_states();
  int Z_scan_order_array_initialization();
  int derivation_z_scan_order_block_availability(int xCurr, int yCurr, int xNbY,
                                                 int yNbY);
  int Up_right_diagonal_scan_order_array_initialization_process(
      int blkSize, uint8_t diagScan[16][2]);

  int Horizontal_scan_order_array_initialization_process(
      int blkSize, uint8_t diagScan[16][2]);

  int Vertical_scan_order_array_initialization_process(int blkSize,
                                                       uint8_t diagScan[16][2]);

  int Traverse_scan_order_array_initialization_process(int blkSize,
                                                       uint8_t diagScan[16][2]);

  int ct_depth = 0;
  uint8_t *tab_ct_depth;
  void set_ct_depth(SPS *sps, int x0, int y0, int log2_cb_size, int ct_depth);

  uint8_t cabac_state[HEVC_CONTEXTS] = {0};
  int StatCoeff[4] = {0};

  int parseSaoMerge();

 public:
  /* 这个编号是解码器自己维护的，每次解码一帧则++ */
  uint32_t slice_number = 0;
  uint32_t CurrMbAddr = 0;

  /* 由CABAC单独解码而来的重要控制变量 */
 public:
  /* 当前宏块是否跳过解码标志 */
  int32_t mb_skip_flag = 0;
  /* 下一宏块是否跳过解码标志 */
  int32_t mb_skip_flag_next_mb = 0;
  uint32_t mb_skip_run = 0;
  int32_t mb_field_decoding_flag = 0;
  int32_t end_of_slice_flag = -1;

 private:
  /* 引用自Slice Header，不能在SliceData中进行二次修改，但是为了代码设计，这里并没有设置为const模式 */
  bool MbaffFrameFlag = 0;
  bool is_mb_field_decoding_flag_prcessed = false;

 private:
  /* 由外部(parseSliceData)传进来的指针，不是Slice Data的一部分，随着parseSliceData后一起消灭 */
  SPS *m_sps = nullptr;
  PPS *m_pps = nullptr;
  /* 由外部(parseSliceData)传进来的指针，不是Slice Data的一部分，随着parseSliceData后一起消灭 */
  SliceHeader *header = nullptr;
  /* 由外部(parseSliceData)初始化，不是Slice Data的一部分，随着parseSliceData后一起消灭 */
  Cabac *cabac = nullptr;
  /* 由外部(parseSliceData)传进来的指针，不是Slice Data的一部分，随着parseSliceData后一起消灭 */
  BitStream *bs = nullptr;
  PictureBase *pic = nullptr;

  int slice_segment_data(BitStream &bs, PictureBase &pic, SPS &sps, PPS &pps);

  int hls_coding_quadtree(int x0, int y0, int log2_cb_size, int cb_depth);
  int ff_hevc_split_coding_unit_flag_decode(int ct_depth, int x0, int y0);
  int log2_res_scale_abs_plus1[32] = {0};
  int res_scale_sign_flag[32] = {0};
  int MaxTbLog2SizeY = 0;
  int MaxTrafoDepth = 0;
  int IntraSplitFlag = 0;
  std::vector<std::vector<uint8_t>> CuPredMode;

  enum PredMode {
    MODE_INTER = 0,
    MODE_INTRA,
    MODE_SKIP,
  };

  int IsCuQpDeltaCoded = 0, CuQpDeltaVal = 0, IsCuChromaQpOffsetCoded = 0;
  enum PartMode {
    PART_2Nx2N = 0,
    PART_2NxN = 1,
    PART_Nx2N = 2,
    PART_NxN = 3,
    PART_2NxnU = 4,
    PART_2NxnD = 5,
    PART_nLx2N = 6,
    PART_nRx2N = 7,
  };
  int CurrPartMode = PART_2Nx2N;

  int ctb_left_flag = 0;
  int ctb_up_flag = 0;
  int ctb_up_right_flag = 0;
  int ctb_up_left_flag = 0;

  enum InterPredIdc {
    PRED_L0 = 0,
    PRED_L1,
    PRED_BI,
  };

  typedef enum {
    IHEVC_CAB_SAO_MERGE = 0,
    IHEVC_CAB_SAO_TYPE = IHEVC_CAB_SAO_MERGE + 1,
    IHEVC_CAB_SPLIT_CU_FLAG = IHEVC_CAB_SAO_TYPE + 1,
    IHEVC_CAB_CU_TQ_BYPASS_FLAG = IHEVC_CAB_SPLIT_CU_FLAG + 3,
    IHEVC_CAB_SKIP_FLAG = IHEVC_CAB_CU_TQ_BYPASS_FLAG + 1,
    IHEVC_CAB_QP_DELTA_ABS = IHEVC_CAB_SKIP_FLAG + 3,
    IHEVC_CAB_PRED_MODE = IHEVC_CAB_QP_DELTA_ABS + 2,
    IHEVC_CAB_PART_MODE = IHEVC_CAB_PRED_MODE + 1,
    IHEVC_CAB_INTRA_LUMA_PRED_FLAG = IHEVC_CAB_PART_MODE + 4,
    IHEVC_CAB_CHROMA_PRED_MODE = IHEVC_CAB_INTRA_LUMA_PRED_FLAG + 1,
    IHEVC_CAB_MERGE_FLAG_EXT = IHEVC_CAB_CHROMA_PRED_MODE + 1,
    IHEVC_CAB_MERGE_IDX_EXT = IHEVC_CAB_MERGE_FLAG_EXT + 1,
    IHEVC_CAB_INTER_PRED_IDC = IHEVC_CAB_MERGE_IDX_EXT + 1,
    IHEVC_CAB_INTER_REF_IDX = IHEVC_CAB_INTER_PRED_IDC + 5,
    IHEVC_CAB_MVD_GRT0 = IHEVC_CAB_INTER_REF_IDX + 2,
    IHEVC_CAB_MVD_GRT1 = IHEVC_CAB_MVD_GRT0 + 1,
    IHEVC_CAB_MVP_L0L1 = IHEVC_CAB_MVD_GRT1 + 1,
    IHEVC_CAB_NORES_IDX = IHEVC_CAB_MVP_L0L1 + 1,
    IHEVC_CAB_SPLIT_TFM = IHEVC_CAB_NORES_IDX + 1,
    IHEVC_CAB_CBF_LUMA_IDX = IHEVC_CAB_SPLIT_TFM + 3,
    IHEVC_CAB_CBCR_IDX = IHEVC_CAB_CBF_LUMA_IDX + 2,
    IHEVC_CAB_TFM_SKIP0 = IHEVC_CAB_CBCR_IDX + 4,
    IHEVC_CAB_TFM_SKIP12 = IHEVC_CAB_TFM_SKIP0 + 1,
    IHEVC_CAB_COEFFX_PREFIX = IHEVC_CAB_TFM_SKIP12 + 1,
    IHEVC_CAB_COEFFY_PREFIX = IHEVC_CAB_COEFFX_PREFIX + 18,
    IHEVC_CAB_CODED_SUBLK_IDX = IHEVC_CAB_COEFFY_PREFIX + 18,
    IHEVC_CAB_COEFF_FLAG = IHEVC_CAB_CODED_SUBLK_IDX + 4,
    IHEVC_CAB_COEFABS_GRTR1_FLAG = IHEVC_CAB_COEFF_FLAG + 42,
    IHEVC_CAB_COEFABS_GRTR2_FLAG = IHEVC_CAB_COEFABS_GRTR1_FLAG + 24,
    IHEVC_CAB_CTXT_END = IHEVC_CAB_COEFABS_GRTR2_FLAG + 6
  } IHEVC_CABAC_CTXT_OFFSETS;

  std::vector<std::vector<uint8_t>> cu_skip_flag;
  std::vector<uint8_t> cu_skip_flag_ctx;
  std::vector<std::vector<uint8_t>> merge_flag;
  std::vector<std::vector<uint8_t>> merge_idx;

  std::vector<std::vector<std::vector<uint8_t>>> split_transform_flag;
  std::vector<std::vector<std::vector<uint8_t>>> cbf_cb;
  std::vector<std::vector<std::vector<uint8_t>>> cbf_cr;
  std::vector<std::vector<std::vector<uint8_t>>> cbf_luma;
  std::vector<std::vector<uint8_t>> rem_intra_luma_pred_mode;
  std::vector<std::vector<uint8_t>> intra_chroma_pred_mode;

  int cu_transquant_bypass_flag = false;
  std::vector<int8_t> qp_y_tab;
  int qp_y = 0;
  int qPy_pred = 0;
  int state_x(int pixel_coord) const;
  int state_y(int pixel_coord) const;
  void reset_block_state_maps();
  void fill_block_map(std::vector<std::vector<uint8_t>> &map, int x0, int y0,
                      int width, int height, uint8_t value);
  int get_qPy_pred(int xBase, int yBase, int log2CbSize);
  void set_qPy(int xBase, int yBase, int log2CbSize);
  void fill_qp_y_tab(int x0, int y0, int log2CbSize);

  /* process表示处理字段，具体处理手段有推流或解码操作 */
  int process_mb_skip_run(int32_t &prevMbSkipped);
  int process_mb_skip_flag(int32_t prevMbSkipped);
  int process_mb_field_decoding_flag(bool entropy_coding_mode_flag);
  int process_end_of_slice_flag(int32_t &end_of_slice_flag);

  int luma_intra_pred_mode(int x0, int y0, int pu_size,
                           int prev_intra_luma_pred_flag);

  int slice_decoding_process();
  inline int decoding_macroblock_to_slice_group_map();
  inline int mapUnitToSliceGroupMap();
  inline int mbToSliceGroupMap();

  /* derivation表示推断字段（根据其他内容进行猜测） */
  int derivation_for_mb_field_decoding_flag();
  int do_macroblock_layer();
  int decoding_process();
  int initCABAC();
  void printFrameReorderPriorityInfo();

 private:
  inline void updatesLocationOfCurrentMacroblock(bool MbaffFrameFlag);

 private:
  /* 用于更新mapUnitToSliceGroupMap的值 */
  int interleaved_slice_group_map_type(int32_t *&mapUnitToSliceGroupMap);
  int dispersed_slice_group_map_type(int32_t *&mapUnitToSliceGroupMap);
  int foreground_with_left_over_slice_group_ma_type(
      int32_t *&mapUnitToSliceGroupMap);
  int box_out_slice_group_map_types(int32_t *&mapUnitToSliceGroupMap,
                                    const int &MapUnitsInSliceGroup0);
  int raster_scan_slice_group_map_types(int32_t *&mapUnitToSliceGroupMap,
                                        const int &MapUnitsInSliceGroup0);
  int wipe_slice_group_map_types(int32_t *&mapUnitToSliceGroupMap,
                                 const int &MapUnitsInSliceGroup0);
  int explicit_slice_group_map_type(int32_t *&mapUnitToSliceGroupMap);
  int get_scf_offse(int &scf_offset, uint8_t *&ctx_idx_map_p,
                    int transform_skip_flag, int cIdx, int log2TrafoSize,
                    int x_cg, int y_cg, int scan_idx, int prev_sig);
  int get_scf_offse0(int &scf_offset, uint8_t *&ctx_idx_map_p,
                     int transform_skip_flag, int cIdx, int log2TrafoSize,
                     int x_cg, int y_cg, int scan_idx, int prev_sig, int i);
};

int NextMbAddress(int n, SliceHeader *slice_header);

#endif /* end of include guard: SLICEBODY_HPP_OVHTPIZQ */
