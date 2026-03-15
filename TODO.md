目标：实现一个简单的VVC解码器，能够解码重构最基本的主流配置码流（YUV420P），不处理色度分量、以及复杂的后处理，滤波等非必要的操作，只是为了熟悉VVC算法（不需要支持什么无损编码模式、兼容模式、按照主流Main profile以及重构像素必要的步骤即可）。

接下来的开发计划：

- [x] **解析码流** ：完成基本的码流NAL解析。
- [x] **系数解码** ：完成基本的frame header解码。
- [ ] **像素系数解码** ：完成像素级别的熵解码。注：目前头部熵解码已稳定，基本没问题。
- [ ] **首个编码像素块重建（Intra，首帧左上角第一个块）** ：完成首个像素块解码，只需要完成Y分量即可。
- [ ] **首行编码像素块重建（Intra，首帧第一行像素块）** ：完成首行像素块解码，只需要完成Y分量即可。
- [ ] **首帧像素重建（Intra）** ：完成首帧解码，只需要完成Y分量即可。
- [ ] **输出与验证** ：将重构首帧的像素输出到 output_first_frame.pgm(Y) 与，自动生成 ref_000.pgm 作为对比。
- [ ] **像素重建（Intra）** ：完成所有帧内解码，只需要完成Y分量即可。
- [ ] **输出与验证** ：将所有帧内重构的像素输出到 output.yuv(Y) 。
- [ ] **首个帧间块像素重建（Inter）** ：完成首个帧间像素块、首个帧间像素行解码，只需要完成Y分量即可。
- [ ] **首个帧间帧像素重建（Inter）** ：完成首个帧间帧解码，只需要完成Y分量即可。
- [ ] **像素重建（Inter）** ：完成帧间解码，只需要完成Y分量即可。
- [ ] **输出与验证** ：将所有帧内、帧间重构的像素输出到 output.yuv(Y) 。

注意：
1. 为了方便这个简单的VVC解码器开发，可以使用vvencapp（本地已安装）编码使用特定工具的码流，从而方便开发或者定位错误，用于编码的源数据可以使用python自己造一个。
2. 目前已有的源代码比较混乱，在开发过程中如果觉得代码错误或者没有实际作用可以进行删除。
3. 测试码流使用demo.h266，测试命令为：./build/a.out demo.h266

demo.h266信息如下：
```sh
[STREAM]
index=0
codec_name=vvc
codec_long_name=H.266 / VVC (Versatile Video Coding)
profile=Main 10
codec_type=video
codec_tag_string=[0][0][0][0]
codec_tag=0x0000
width=714
height=624
coded_width=714
coded_height=624
has_b_frames=1
sample_aspect_ratio=N/A
display_aspect_ratio=N/A
pix_fmt=yuv420p10le
level=51
color_range=tv
color_space=unknown
color_transfer=unknown
color_primaries=unknown
chroma_location=unspecified
field_order=unknown
refs=1
id=N/A
r_frame_rate=60/1
avg_frame_rate=25/1
time_base=1/1200000
start_pts=N/A
start_time=N/A
duration_ts=N/A
duration=N/A
bit_rate=N/A
max_bit_rate=N/A
bits_per_raw_sample=N/A
nb_frames=N/A
nb_read_frames=N/A
nb_read_packets=N/A
extradata_size=268
DISPOSITION:default=0
DISPOSITION:dub=0
DISPOSITION:original=0
DISPOSITION:comment=0
DISPOSITION:lyrics=0
DISPOSITION:karaoke=0
DISPOSITION:forced=0
DISPOSITION:hearing_impaired=0
DISPOSITION:visual_impaired=0
DISPOSITION:clean_effects=0
DISPOSITION:attached_pic=0
DISPOSITION:timed_thumbnails=0
DISPOSITION:non_diegetic=0
DISPOSITION:captions=0
DISPOSITION:descriptions=0
DISPOSITION:metadata=0
DISPOSITION:dependent=0
DISPOSITION:still_image=0
DISPOSITION:multilayer=0
[/STREAM]
```


目前进展（开发总结）：
1. [2026-03-13 23:39:40 CST] 已完成第一个开发计划“解析码流”：打通 VVC Annex B 码流的基础 NAL 解析，支持 `.h266/.vvc` 输入、VVC 2-byte NAL 头字段解析、RBSP 提取、NAL 类型识别与统计汇总；`demo.h266` 实测可稳定解析出 14 个 NAL（SPS/PPS/Prefix APS/IDR_W_RADL/RADL/STSA）。
2. [2026-03-14 00:01:30 CST] 已完成第二个开发计划“系数解码”：在复用现有 `AnnexBReader + Nalu + RBSP` 主循环的前提下，新增最小 VVC frame header 解析器，完成 SPS/PPS 基本语法、picture header、slice header 前部和 POC 推导；`demo.h266` 实测可稳定输出每个 VCL NAL 的 `pps_id/sps_id/ph_in_sh/poc_lsb/poc/slice_type` 摘要，并与本地 `ffmpeg trace_headers` 抽查结果一致。
3. [2026-03-14 11:22:22 CST] 第三个开发计划“像素系数解码”已完成前置准备：继续复用现有 `RBSP + BitStream` 路径，补齐了 slice header tail 的 demo 子集解析，新增 SPS/PPS 的分块约束、ALF/SAO、依赖量化、sign data hiding、slice QP 等关键字段，并明确暴露了切片 payload 起点的两种偏移口径：`payload_byte_offset` 对齐 `ffmpeg trace_headers` 的 NAL 口径，`payload_rbsp_byte_offset` 保留后续直接接入 VVC CABAC/残差解码所需的 RBSP 口径；`demo.h266` 实测首个 IDR 与前几个 RADL 切片的 `qp/depq/payload_byte` 已可稳定对齐参考值。第 3 项暂未完成，下一步进入实际 CABAC/像素系数解析。
4. [2026-03-14 11:32:48 CST] 第三个开发计划继续推进：新增独立的最小 `VvcCabacReader`，先复用当前 HEVC 工程里已有的 ffmpeg 风格算术引擎初始化思路，但保持实现与 HEVC 路径解耦；VVC 主路径现在会基于 `payload_rbsp_byte_offset` 对每个切片实际完成一次 CABAC 引擎起始装载校验，确保 slice header 到 slice_data 的切分结果已可直接衔接后续的 VVC 系数/语法熵解码。
5. [2026-03-14 14:15:34 CST] 第三个开发计划继续推进：将 `VvcCabacReader` 升级为可执行 VVC 上下文 bin 解码（新增 context model 初始化/更新与 `decodeBin`，保留 `decodeBypass/decodeTerminate`）；在此基础上新增最小 `VvcSplitProbe`，先针对首个 I-slice 的首个 CU 递归解码分割语法路径（`split_flag/split_qt_flag/split_hv_flag/split_12_flag` 子集，参考同目录 `vvdec` 的上下文初始化与分割判定流程），主程序现可输出 `split_path/split_leaf/split_bins` 作为下一步接入 `pred_mode/intra_luma_mode/残差语法` 的前置验证信息。`demo.h266` 当前实测可稳定输出 `split_path=N`（首块未继续分割）的探针结果。
6. [2026-03-15 18:10:56 CST] 第三个开发计划继续推进：新增最小 `VvcIntraCuProbe`，继续沿用当前 `VvcCabacReader + VvcSplitProbe` 的首块链路，在 `split_path` 之后补上首个 Intra 叶子 CU 的预测语法探针，已覆盖 `mip_flag / mip_pred_mode / multi_ref_line_idx / isp_mode / intra_luma_pred_mode` 这条最小路径，并将结果接入主程序输出。`demo.h266` 当前实测首个叶子 CU 为 `128x128`、`split_path=N`、`first_cu_luma=1`（DC）、`first_cu_src=mpm[1]`，同时确认首块 **不是** `MIP/MRL/ISP` 分支。基于这个结果，当前最主要的问题已经收敛为：`split` 之后的 `transform_tree / transform_unit / residual_coding` 仍未实现，这正是“首个编码像素块重建（Intra）”的直接阻塞点。
7. [2026-03-15 18:18:18 CST] 第三个开发计划继续推进：补齐了最小 `VvcTransformTreeProbe`，并顺手把 SPS 中此前未保存但后续 TU/残差语法需要的 `log2_max_transform_skip_block_size` 与 `cclm_enabled_flag` 也接入了解析状态；当前主路径已能在首个 Intra CU 的预测语法之后，继续探到首个 leaf TU 的最小 `transform_tree / transform_unit / residual` 前缀信息。`demo.h266` 当前实测结果进一步收敛为：首个 CU 会因 `max_tb_size` 限制隐式分裂到 `first_tu=64x64`，且 `first_tu_cbf_y=1`、`first_tu_cbf_cb=0`、`first_tu_cbf_cr=0`、`first_tu_last_sig=(12,16)`。这说明当前最主要的问题已经进一步收窄到：**首个 64x64 luma TU 的 `residual_coding_subblock / coeff level / 反量化逆变换` 仍未实现**；换句话说，TU 层级和最后非零系数位置已经能摸到，下一步应直接进入系数显著图与系数值解码。
8. [2026-03-15 18:31:17 CST] 第三个开发计划继续推进：将 `VvcResidualProbe` 从“只探 `significant_coeffgroup_flag`”扩展到首个 TU 的最小 `residual_coding_subblock` 实解，现已覆盖首个 `64x64` luma TU 的 `sig_coeff_group_flag / sig_flag / gt1 / parity / gt2 / rem_abs / sign` 这条非 transform-skip 残差路径，并把 `dep_quant_used_flag` 与 `sign_data_hiding_used_flag` 接入了探针；主程序输出也同步增加了首个 TU 的非零系数统计。`demo.h266` 当前实测更新为：`first_tu_scan_pos_last=496`、`first_tu_last_subset=31`、`first_tu_sig_groups=25`、`first_tu_first_sig_subset=0`、`first_tu_nonzero=303`、`first_tu_abs_sum=2003`、`first_tu_max_abs=63`、`first_tu_last_coeff=2`。这说明当前最主要的问题已经继续收敛为：**首个 TU 的系数熵解码基本已打通，但 `反量化 / 逆变换 / intra 预测样本生成 / 残差叠加重建` 仍未实现**；也就是说，阻塞“首个编码像素块重建（Intra）”的核心已从 CABAC 系数语法转移到重建链路本身。
9. [2026-03-15 18:43:22 CST] 第三个开发计划继续推进：新增最小 `VvcReconstructionProbe`，在当前 `首个 CU = 128x128 / 首个 TU = 64x64 / intra_luma_mode = DC / MRL=0 / ISP=0 / TS=0` 的已知前提下，先打通首个 top-left `64x64` luma TU 的最小重建链路；实现内容包括：补齐 SPS `bit_depth` 状态、按当前 `slice_qp_y + qp_bd_offset` 执行最小 dequant、基于本地 `vvdec` 的 `DCT2 64x64` 变换矩阵做 2D inverse transform、以及在 top-left 无可用参考样本时使用中值参考构造 DC predictor 后完成残差叠加与裁剪。由于当前首个 CU 宽高为 `128x128`，因此 `LFNST` 在该 CU 上不会被信令，`MTS` 也因超出 `32x32` 限制而不会启用，所以这条最小重建路径对 `demo.h266` 是成立的。`demo.h266` 当前实测新增结果为：`first_tu_pred_dc=512`、`first_tu_resi_min=-145`、`first_tu_resi_max=441`、`first_tu_recon_min=367`、`first_tu_recon_max=953`、`first_tu_recon_sum=2107520`、`first_tu_recon_tl=953`、`first_tu_recon_c=552`、`first_tu_recon_br=440`。这说明当前最主要的问题已经从“首个 TU 的重建链路缺失”继续转移为：**需要把当前“只覆盖首个 `64x64` TU”的重建扩展到同一 `128x128` Intra CU 的剩余 3 个 sibling TU，并进一步建立后续块所需的左/上参考像素依赖与逐块重建缓存**；换句话说，下一步阻塞点已经不再是单 TU 的 CABAC/逆变换，而是从“首个 TU”迈向“首个完整块/首行块”的真实块级解码流程。
10. [2026-03-15 19:05:40 CST] 用本地 `vvdec-3.2.0/` 的 `D_SYNTAX`/`D_HEADER` trace 重新校对 `demo.h266` 后，确认此前第 5-9 条建立在错误前提上：真实参考实现里，首个原点区域并不会在我们当前输出的 `split_path=N` 处停止，而是还会继续发生多次 `split_cu_mode()`；也就是说，我们目前的首块 `split/intra/TU/residual/reconstruction` 探针仍然没有站在正确的 CABAC 位置上。进一步做一次性偏移诊断发现：保持现有 CABAC 引擎不变，仅把当前 `payload_rbsp_byte_offset=7` 往前试 1 个字节到候选 offset `6`，首个 `split_flag` 就会从 `0` 变成与 `vvdec` 一致的 `1`，同时 `VvcSplitProbe` 的结果也会从 `N` 变成至少 `Q>N`。这说明当前最主要的问题已经重新收敛为：**slice header 尾部到 `slice_data` 的 RBSP payload 切分仍然错位，而且当前 `VvcSplitProbe` 对真实 split 上下文/遍历顺序的建模也过于简化**。本轮已补了 `entry point offsets` 的最小跳过逻辑，但 `demo.h266` 的 `sps_entropy_coding_sync_enabled_flag=0`，因此它并不是这条码流里导致错位的根因。下一步应优先修正 `payload_rbsp_byte_offset` 与 `split_cu_mode` 的真实路径，再重新验证 Intra/TU/残差/重建链路；在此之前，第 5-9 条的块级探针结果只能视为探索性结果，不能再当作真实解码进度。
11. [2026-03-15 19:14:32 CST] 继续对照 `vvdec` 后，又把 `VvcCabacReader::decodeBin()` 改写成更贴近 `vvdec BinDecoder` 的版本，并重新回归 `demo.h266`。结果首个 slice 的输出仍然停留在 `split_path=N`，说明问题并不只是此前自写 CABAC `decodeBin()` 的分支实现差异；更可能的真实阻塞点仍然在于：**我们目前对“首个 split bin 的上下文条件 / split 遍历状态 / 或 slice_data 起点”的整体建模还不对，因此即便 CABAC 单 bin 算法更接近参考实现，也还是在错误的位置上读到了错误的 `split_flag`**。换句话说，当前第一优先级仍然是把首个 `split_cu_mode()` 的输入条件和遍历过程对齐到 `vvdec` trace，而不是继续在错误 split 路径上扩展 TU/重建逻辑。
