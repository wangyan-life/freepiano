收到任务 —— 我将给出一个自下而上、最小实现导向的重构设计与实施建议，包含契约、数据格式、线程模型、跨语言接口（C DLL <-> C#）、实现步骤和验收/测试要点，方便你按阶段推进并在 CI/本地验证每一步。

检查清单（我将覆盖每项）
- 提取并确认需求：C++ 实时核心（MIDI、音频、VSTi/SF2、实时音符处理），C# GUI 与非实时功能。 Done
- 设计二进制数据格式（MIDI 事件、配置）并给出布局。 Done
- 线程模型与实时 / 非实时隔离方案（锁/无锁队列、回调合同）。 Done
- C++ DLL 的 C 风格接口契约与示例签名（供 C# P/Invoke 调用）。 Done
- 推荐的第三方库、实现细节与权衡（ASIO/WASAPI、VST、SF2）。 Done
- 最小实现分阶段计划（逐步可测、低风险），含测试/质量门。 Done

高层计划（1 行）
- 先实现最小可跑的实时核心（音频驱动 abstraction + 简单合成 + MIDI 输入通道），暴露 C API；验证低延迟后逐步接入 SF2/VST、扩展 MIDI 路由，再做 C# GUI 与持久化。

工程契约（紧凑）
- 输入：二进制 MIDI 事件流（结构化），外部命令（非实时配置、加载/卸载乐器/插件）。
- 输出：立刻填充的音频回调缓冲（32-bit float interleaved），日志/状态通过非实时通道上报。
- 错误模式：设备丢失、缓冲 underrun、插件崩溃、格式不匹配。 实现要求在实时线程不抛异常、不分配、不加锁。
- 成功准则：在目标硬件上端到端 roundtrip latency 可测且低（例如 < 10 ms）；MIDI->声音路径稳定无跳音。

关键设计要点（要点式）
- 实时安全原则：音频回调 / MIDI realtime 线程绝不调用堆分配、文件 IO、锁（除非非阻塞且短）或托管边界；所有长耗时操作在后台线程执行。
- 通信：用单生产者单消费者（SPSC）无锁环形缓冲用于实时事件（MIDI、控制变更），用另一个队列传递非实时命令/回复；C# 与 C++ 之间通过 P/Invoke 调用 C 接口，非实时通信和 UI 状态通过回调或轮询拉取。
- 数据格式：结构化、内存对齐、固定长度头（避免 JSON）
  - 示例 MIDI 事件（二进制）：
    - struct MidiEvent { uint64_t timestamp_us; uint8_t status; uint8_t data[2]; uint8_t data_len; uint8_t flags; /* pad */ };
  - 所有多字节字段采用 little-endian（Windows 默认），并使用固定宽度类型（uint64_t, uint32_t）。
- C API 约定：显式 init/start/stop/close，音频回调在 C++ 内部由驱动调用（C# 不直接进入回调）。
- 内存：为 ring buffer 和对象池预分配对象避免实时分配；使用 aligned_alloc/posix_memalign 或 Windows VirtualAlloc 根据需要。

跨语言接口（核心示例）
- 推荐的头部原型（C 风格，放 `cpp/include/core_api.h`）：
```c
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*StatusCallback)(int code, const char* msg, void* ctx);

// 初始化 / 释放
int fp_core_init(const char* config_bin_path, StatusCallback cb, void* cb_ctx);
int fp_core_start(void);
int fp_core_stop(void);
int fp_core_shutdown(void);

// MIDI 输入（非实时线程可以调用，但会写入 SPSC 队列；实时线程消费）
typedef struct { uint64_t timestamp_us; uint8_t status; uint8_t data[2]; uint8_t data_len; uint8_t flags; } fp_midi_event_t;
int fp_core_push_midi(const fp_midi_event_t* ev);

// 非实时命令（加载 SF2 / VST、保存配置）
int fp_core_enqueue_command(const void* cmd_bin, uint32_t cmd_len);

// 查询状态
int fp_core_poll_status(char* out_buf, uint32_t buf_len);

#ifdef __cplusplus
}
#endif
```
- C# 侧使用 DllImport，避免在 P/Invoke 中传递大型结构频繁调用，尽量批量推送事件或使用共享内存 / memory-mapped file（可选）。

线程模型（简图/职责）
- Audio RT Thread (C++) — 音频驱动回调；从 MIDI SPSC 读取事件、驱动 synth/render；绝不阻塞。
- MIDI I/O Thread (C++) — 接收外部 MIDI（RtMidi 或 Windows MIDI API），写入 MIDI SPSC。
- Worker Thread(s) (C++) — 插件加载、文件 IO、资源初始化（非实时）。
- UI Thread (C#) — 处理绘制、动画、用户交互，通过命令队列与 C++ 通信。
- Bridge：C API + SPSC；状态回调（非实时）用于通知 UI。

队列与并发实现建议
- 实时队列：实现自定义 ring buffer（固定容量 power-of-two，索引原子操作），或使用 well-known lock-free SPSC 实现（轻量自写优于外部依赖，确保内存屏障正确）。
- 非实时队列：可用 std::mutex + std::deque，因为非实时线程容忍锁。
- 避免：在实时线程使用 std::shared_ptr 的引用计数（可能导致原子开销），避免虚函数间接开销频繁调用。

音频/设备与第三方库建议（权衡）
- 音频驱动：
  - Windows 专注：WASAPI（共享与独占）与 ASIO（低延迟但需要 SDK/驱动）；
  - 推荐封装：写抽象层 `IAudioDriver`：实现 `WASAPIDriver`、`ASIODriver`。可选使用 RtAudio/PortAudio 以节省跨平台实现代价，但若只需 Windows，可直接用 WASAPI/ASIO。
- MIDI I/O：
  - 推荐使用 RtMidi（跨平台、简单）或 Windows 原生 MIDI API (WinMM/Windows MIDI)、或 WASAPI MIDI（Windows 10+）。
- VSTi 插件宿主：
  - 推荐目标：VST3（官方 SDK）。VST2 已不可用授权，保持兼容层时需注意许可问题。
  - 提示：宿主需要隔离插件的异常与堆栈（插件崩溃保护）；建议把插件运行在单独线程/进程或使用 try/catch + Windows structured exception handling 做防护。
- SoundFont (SF2)：
  - 推荐使用 `FluidSynth` 或 `sfizz`（现代 C++ 实现），或自写轻量 SF2 解析器（代价高）。FluidSynth 简单易用但依赖较多；sfizz 对性能和现代有利。
- VST + SF2 优先次序：先实现 SF2 音源（更可控、免插件崩溃问题），再接入 VST 宿主作为扩展。

实时音符处理（功能点）
- Map MIDI note -> voice allocation（声部管理）、移调（semitones）、力度映射（curve table），以及 pitchbend/aftertouch。
- Voice 管理：固定声部池（预分配 voice objects），支持 voice stealing 策略（oldest/lowest-priority）。
- 动态参数映射在实时路径用预计算 LUT（避免浮点开销）或尽量少计算。

最小实现分阶段路线（每步均可验证）
1) 基础类型与队列（1-2 天）
   - 实现 `fp_midi_event_t`、SPSC ring buffer、C API skeleton。
   - 本地单元测试：在本进程模拟推送/消费事件并断言顺序与无丢失。

2) 基本音频驱动 + 合成器（1 周）
   - 实现 `IAudioDriver` for WASAPI（共享模式优先），实现一个简易 sine / square synth 渲染 MIDI note-on/off。
   - 验证：音频回放无裂音，测量 roundtrip latency（用 scope 或 loopback）。

3) MIDI I/O（2-3 天）
   - 接入 RtMidi 或 Windows MIDI，确保外部设备事件能传入 SPSC 并触发声音。
   - 验证：外部 MIDI 控制器能触发声音且延迟满足目标。

4) C API + C# 简单 GUI（1 周）
   - 暴露 init/start/stop/push_midi；C# 做基本键盘 UI（发送 MIDI 事件到 DLL）。
   - 验证：C# GUI 能控制合成器，键盘动画和声音同步（主观感受 +测量）。

5) SF2 支持 + 声部管理（2-3 周）
   - 集成 FluidSynth 或 sfizz，替换测试合成器。
   - 验证：加载 SF2，播放多声部，无崩溃。

6) VST 宿主（可选，复杂 2-6 周）
   - 使用 VST3 SDK，实现基本宿主 API，确保插件异常隔离与线程安全。
   - 验证：加载常见 VSTi（只测试已授权的 VST）。

7) 配置保存/加载、MIDI 文件导入/导出、持久化（1 周）
   - 使用二进制格式或轻量 protobuf（如果需要版本化），但遵照“二进制最小解析开销”原则。
   - 验证：跨会话加载设备/layout 并能恢复状态。

质量门与测试（持续）
- 单元测试：队列、midi parser、voice allocator。
- 集成测试：本地 loopback 测试（MIDI in -> audio out），自动化脚本可跑在 CI Windows runner（需要音频设备虚拟化或模拟）。
- 性能：测量音频回调执行时间分布，确保 < buffer duration/2。
- 安全：插件隔离与 crash-recovery 策略。

迁移与 repo 结构建议（以便分阶段提交）
- 建议目录：
  - `cpp/core/` — 实时核心（drivers, synth, midi, api）
  - `cpp/include/` — public C header (`core_api.h`)
  - `csharp/gui/` — C# WPF/WinForms 前端
  - `tools/` — 小工具（latency test, log）
  - docs — 接口与运行说明
- 初始最小提交：`cpp/include/core_api.h` + `cpp/core/spsc_ring.h/.cpp` + `cpp/core/audio_driver_stub` + test harness。
- CI：添加 Windows build job（msbuild）并在合并主干前要求通过基本集成 smoke test（非声卡依赖的单元/仿真测试）。

风险与缓解（3 个主要风险）
- 插件崩溃导致主进程崩溃：隔离插件（子进程或 sandbox）或至少保守的 exception/SEH 捕获。
- ASIO SDK/驱动许可与封闭源：在设计中把 ASIO 抽象为可选模块，默认使用 WASAPI。
- 实时内存或锁导致 dropouts：严格遵守实时安全规则，使用预分配和无锁结构，并把回调时间监控纳入日志。

操作样例（C# 调用 C API 的简短示例）
```csharp
[DllImport("freepiano_core.dll", CallingConvention = CallingConvention.Cdecl)]
static extern int fp_core_init(string configPath, StatusCb cb, IntPtr ctx);

[StructLayout(LayoutKind.Sequential)]
struct fp_midi_event_t { ulong timestamp_us; byte status; byte data0; byte data1; byte data_len; byte flags; /*pad*/ }

[DllImport("freepiano_core.dll")]
static extern int fp_core_push_midi(ref fp_midi_event_t ev);
```
（注意：在实际实现中，建议 C# 批量推送或通过共享内存降低 P/Invoke 频率）

最后的建议与下一步
- 优先从最小可运行“WASAPI + 简单合成 + SPSC + C API + C# demo”做起；在该阶段你能快速验证实时链路与跨语言桥接是否稳健。
- 我可以为你起草：
  - 一个精确的 `cpp/include/core_api.h` 文件（已给出示例），
  - SPSC ring buffer 的高质量实现草案（带单元测试），
  - 一个 WASAPI 最小驱动实现样板（用于立即测试）。
选择其中一项，我会继续生成对应的具体代码与测试用例（并在本地/CI 下做快速 smoke test 指南）。