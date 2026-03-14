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
