# PathBuilder 首个兼容版本实施方案

## 目标与发布边界

先发布 `skity::PathBuilder` 和共享几何存储，再迁移内部及业务代码，最后删除
`Path` 的几何修改接口。本版本保留现有 C++ 方法签名和 C API，不默认增加
`deprecated` 警告，避免使用方的 `-Werror` 阻断升级。

这是重新编译后的源码兼容，**不是 C++ ABI 兼容**。`Path` 的布局变化，Skity、
C API/IO 等适配模块以及所有直接使用 C++ 接口的团队必须使用同一版头文件重编。
不能只替换动态库。C 的 opaque handle 和函数签名保持不变。

`PathBuilder` 属于默认构建（`SKITY_UNIFIED_LIB=OFF`）的 C++ 接口。统一库构建
继续只导出既有 C ABI，不安装这些 C++ 头文件；C API 的路径复制同样受益于内部
几何共享，本提交不增加 C API 或统一库的 C++ wrapper 接口。

## 首版实现

- `Path` 的 points、verbs、conic weights 放入一个内部引用计数数据对象。空路径共享
  不参与引用计数的空节点；复制、赋值和录制持有 Path 时不复制几何数组。
- 旧几何修改方法在写前检查所有权；独占时原地修改，共享时复制一次再修改。
  填充规则、形状类型等轻量元数据仍属于各个 Path 值，修改它们不复制数组。
- 引用递增使用 relaxed，释放使用 acq_rel，独占判断使用 acquire，让最后一个共享
  读者的访问先于恢复独占后的写入。`EnsureUnique()` 和 `Reset()` 使用同一判断。
  不使用缺少这一同步保证的 `shared_ptr::unique()` / `use_count()`。
- bounds/finite 缓存与几何数据一起共享，通过锁和原子完成标记保护冷计算，热读
  不加锁。凸性及方向缓存使用原子字段。允许同一个 Path 的并发 const 查询；
  同一个对象的修改仍需调用方同步。
- `PathBuilder` 在首版内部持有独占的 Path，调用同一套构建算法，不复制 Arc、
  Conic、RRect 等实现。它不提供可修改的 Path 引用。删除旧修改 API 时，再把这套
  算法移到 Builder，不要求业务再次改代码。
- `Detach()` 转移已有数组并将 Builder 恢复为空、默认 winding；不复制几何。
  转移出去的容量归返回的 Path 所有，Builder 下一次构建需要重新获取容量。
- `Snapshot()` 复制几何并保留 Builder 的内容和容量。返回值与 Builder 独立，
  后续构建不触发因该快照造成的写时复制。
- `Reset()` 清空并保留独占数组的容量。旧 Path 若已共享则直接释放自身引用，
  不复制即将丢弃的几何。Builder 从 Path 构造或复制 Builder 时显式复制几何。
- 保持按内容比较的 `operator==`、变换结果、特殊形状识别和迭代器借用约定。
  指针和迭代器只在原对象存活且未修改时有效；共享存储不扩大这个约定。
- 变换路径按实际点数预留空间，避免共享后继承源 vector 的多余容量和重复的小数组分配。
- `AddPath()` 按所需点数和目标容量的倍增值扩容，不继承源路径的闲置容量。
- 覆盖自追加、两个值共享后追加、反向追加、Reset、Swap、移动后复用，以及
  绘制/录制后修改原始 Path 的值语义。

```cpp
#include <skity/graphic/path_builder.hpp>

skity::PathBuilder builder;
builder.MoveTo(0, 0).LineTo(100, 0).LineTo(100, 100).Close();
skity::Path path = builder.Detach();
canvas->DrawPath(path, paint);

skity::Path edited = path;  // 共享几何，不分配几何数组
edited.LineTo(200, 200);    // 兼容接口：写前分离，path 不变

skity::PathBuilder edit(path);  // 显式复制，编辑已有路径
edit.LineTo(50, 50);
skity::Path next = edit.Detach();
```

构建新接口的收益来自共享 Path 的传递、零几何复制的 Detach，以及清晰的构建生命周期。
旧用户不改源码也会得到 Path 复制变轻的收益；频繁“复制后修改”的旧模式仍需复制几何。

## 与 Skia 的取舍

参考 issue [#296](https://github.com/lynx-family/skity/issues/296) 和本地 Skia
`include/core/SkPathBuilder.h`、`src/core/SkPathBuilder.cpp`、`src/core/SkPathData.cpp`。
Skia 当前 Builder 使用可复用数组，`snapshot()` 创建紧凑数据，`detach()` 是
snapshot 后 reset，并不零复制。Skity 首版保留 vector，以最小改动实现转移；
紧凑单块存储、小数组优化和跨帧 Builder 容量策略应在真实负载测量后决定。
独占判断参考 `include/core/SkRefCnt.h` 的 acquire 语义。引用计数仅封装在
`Path::Data` 内部，不新增通用智能指针或公开所有权 API。

## 影响范围

1. `path.hpp` / `path.cc`：共享存储、旧接口分离、缓存及值语义。
2. 新公开 `path_builder.hpp` 及实现：完整构建接口与 Detach/Snapshot/Reset。
3. CMake 公开头文件、源码清单与 `skity.hpp` 导出。
4. Path/Builder 单测、录制回放测试和现有 GL golden 回归；用 Builder 构造真实
   golden 路径，以验证新入口实际进入 ContourAA/CoverageAA。
5. C API、IO、字体、PathEffect、Stroke、Canvas 等现有调用方保持源码可编译，
   本版不批量迁移。`module/io/src/io/core_bridge.hpp` 中的同名包装器属于另一命名
   空间，迁移时须一起处理其 C handle/Peek/Take 约定。
6. `test/bench/micro/path_benchmarks.cc`：空路径、构建、复制、Reset 后复制交付、
   Builder Detach/Snapshot 的单线程和 8 线程基准，以及保留大容量后的追加基准。

## 后续迁移

- 发布兼容库并让内部团队重编升级。
- 按模块将构建点换成 Builder，保持 Canvas/DisplayList 的 Path 输入不变。优先
  Stroke/PathEffect、字体和解析器，再迁移业务路径构建；动态路径显式选择
  Snapshot 保留容量或 Detach 转移数组。
- 完成迁移后再引入弃用诊断；确认 C API、IO 桥接和外部调用方均不依赖旧接口后，
  在约定的破坏性版本删除几何修改方法。赋值、Swap 等值替换操作可保留。

## 验收与性能范围

所有测试通过 `tools/test-runner.py`，读取其 `agent_test_report.json`。
先运行 Path/Builder/DisplayList 等最小单测，再扩展单测与 GL shape golden。
测试需要证明共享数组、写前隔离、Builder 生命周期、变换/特殊形状元数据、
并发 const 查询、录制快照以及新旧入口的渲染一致。

本版不加入几何缓存 ID、网格缓存、GL buffer/texture 池，也不改变 AA 算法。
它消除普通 Path 拷贝的数组分配，不消除 CoverageAA 的坐标变换、逐帧 tessellation、
CPU staging 上传或驱动同步。GL/iOS 的最终帧耗时收益仍需独立测量。

## 实施与验证记录

首版代码已实现，新增接口已进入公开头文件与构建清单，旧调用方保持可编译。
同步修复 `Swap` 遗漏填充规则/方向、闭合路径追加的起点偏移，并在几何编辑后
使凸性/方向缓存失效。C API 测试的越界枚举哨兵改为合法枚举，避免测试自身触发 UBSan。

本提交基于上游 `main`（`865f9fa`）。2026-10-28 在该基线上重新运行全量单测、
GL golden、ASan/UBSan、TSan，并验证统一库构建。分配和微基准数据来自前一轮
相同 PathBuilder 实现的测量。

| 验证 | 结果 |
| --- | --- |
| 全量 unit，包含 C API/IO 调用方重编 | 1035 / 1035 通过 |
| GL shape golden：曲线、填充规则、描边、clip、录制变换及两种 AA 的 Builder 用例 | 19 / 19 通过 |
| ASan + UBSan：Path、Builder、Coverage tiler、录制、字形与 C API | 141 / 141 通过 |
| TSan：共享 Path、Builder、最后读者释放后编辑及 Reset | 13 / 13 通过 |
| `SKITY_UNIFIED_LIB=ON`：C API 单测，包含路径克隆值语义 | 11 / 11 通过 |
| 分配契约和保留容量追加探针 | 2 / 2 通过 |
| 新增微基准运行检查 | 24 个参数组合全部完成 |

分配契约探针确认：预热后 Path 拷贝/赋值、Builder Detach、容量足够的
Reset 后重建、共享 Path 的 Reset 均为零 malloc/calloc/realloc；Snapshot 与
首次共享后修改会分配，随后原地 SetLastPt 不再分配。

审查发现并修复三处问题：恢复独占时缺少同步、空节点引用计数争用、追加传播闲置容量。
TSan 对修复前的最后读者释放场景报告数据竞争，修复后编辑和 Reset 两个分支均通过。
空节点的构造、复制、移动和销毁均不更新全局计数。先构建 65536 点、Reset 后构建
8 点再复制追加的探针，追加阶段累计申请量由 1,048,940 B 降至 412 B。

相同机器上 Release 构建、五轮中位数的微基准结果如下。8 线程的数值是总耗时除以
总操作数，表示吞吐成本；各线程独立构建自己的路径，测量期间没有其他本任务构建或测试。

| 操作 | 原始 Path | 最终兼容版本 |
| --- | ---: | ---: |
| 四点 Path 复制，单线程 | 37.2 ns | 4.4 ns |
| 复用容量构建四点并复制交付，单线程 | 42.9 ns | 23.7 ns |
| 一次性构建四点，单线程 | 65.0 ns | 86.5 ns |
| 独立构建四点，8 线程 | 28.2 ns | 25.8 ns |
| 构造/销毁空路径，8 线程 | 16.7 ns | 0.32 ns |
| 四点路径累计申请字节 | 120 B | 280 B |
| `sizeof(Path)`，本机 arm64 | 136 B | 48 B |

收益集中于复制和传递；一次性小路径的构建和固定分配开销仍高于原始实现。
`Detach()` 不复制几何，但不会保留 Builder 的容量，因此不能据此推断逐帧构建无分配。

以下真实 GL 数据来自审查修复前的兼容实现初测，保留用于说明性能边界，不作为最终
版本的帧率结论。实际场景保持旧调用代码不变，预热 16 帧后采集一帧的渲染线程分配。
计数包含该线程上的驱动调用，字节数为累计申请量，不代表峰值常驻内存或 GPU 内存。

| 场景 | AA | 申请次数 前 → 后 | 申请字节 前 → 后 |
| --- | --- | ---: | ---: |
| Tiger1000 | ContourAA | 11000 → 9246（−15.9%） | 1804560 → 1592376（−11.8%） |
| Tiger1000 | CoverageAA | 9189 → 6829（−25.7%） | 1537368 → 1330080（−13.5%） |
| CanvasMark Paths | ContourAA | 100538 → 84918（−15.5%） | 10160948 → 10305284（+1.4%） |
| CanvasMark Paths | CoverageAA | 90784 → 71282（−21.5%） | 9851500 → 10029644（+1.8%） |

共享数据对象及同步缓存增加了每个独立几何对象的固定开销，因此申请次数减少不保证
申请字节同步减少。首版保留这一取舍；大量一次性小路径仍是后续紧凑存储优化的对象。

初测原生 GL 在 Apple M5 Max、GL 4.1 Metal - 90.5 上测试。每配置预热 16 帧、测量
64 帧，最终轮测量期间没有并行编译或其他本任务渲染测试。当前上传策略下，
六个场景/AA 组合的 CPU 帧中位数变化约 −2.1% 到 +0.2%；只有一轮前后对比，
不能据此确认稳定的帧率提升。48 配置使用完整 RGBA8 帧的 FNV-1a 64 位哈希比较，
不是逐字节证明。GL golden 使用仓库既有比较阈值，未更新基准图。

初测原始结果保存在 `build/path-builder-study/`（忽略目录）：
`full-unit.json`、`gl-golden.json`、`asan.json`、`allocation-contracts.json`、
`allocation-results.json`、`native-gl.json`、`native-gl-results.csv`。

最终验证和微基准结果保存在 `build/path-builder-review/fixed/`：
`full-unit.json`、`gl-golden.json`、`asan.json`、`tsan.json`、`allocation-contracts.json`、
`allocation-gtest.json`、`baseline-timings.json`、`timings.json`。
上游 main 基线的复验结果保存在 `build/path-builder-review/main/`：
`full-unit.json`、`gl-golden.json`、`asan.json`、`tsan.json`、`unified-unit.json`。

扩大 sanitizer 范围时，现有 `Arena::Allocate()` 首次分配中的空指针偏移触发 UBSan；
该代码未在本提交修改，报告保存在 `asan-broad-initial.json`。上述 141 项相关用例
在 ASan/UBSan halt-on-error 模式下通过。TSan 同样设置 halt-on-error，避免错误恢复
后仅依据 gtest 断言结果判定通过。

发布前仍需 iOS GLES 真机性能与集成验证；未运行 Android/Windows 构建或全量
golden-text。本次没有发布二进制或迁移业务仓库，C++ 使用方升级时必须统一重编。
