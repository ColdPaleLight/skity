# 硬件 Tess Stroke Matrix Buffer 与合批设计

## 背景

`WGSLTessPathStrokeGeometry` 是硬件 tessellation 合批改造的第一个目标。
当前 stroke 绘制有几个问题会阻碍后续合批：

1. transform 仍然放在 `CommonSlot.userTransform` 这种 per-draw uniform
   数据里。两个 stroke 只要 matrix 不同，就很难进入同一个批次。
2. stroke 目前仍然是 stencil pass + cover pass。即使 geometry 和 paint
   状态一致，一个逻辑 stroke 也会拆成两段命令，天然不利于合批。
3. draw-time 的 `Draw`、`Geometry`、`Fragment` 对象同时承担静态 pipeline
   生成和动态数据填充。它们不是面向合批的数据结构，每次绘制都会重新创建
   一批对象。

第一阶段应该收得很窄：先让 Metal 路径把 stroke transform 从 uniform
语义迁到 storage buffer/SSBO 语义，并用 golden test 确认输出一致。只有这
一步跑通以后，再继续扩展到 transform array、stroke 合批，以及后续的
depth-test 单 pass。

## 目标

- `WGSLTessPathStrokeGeometry` 的 transform 不再从 `CommonSlot` 读取，而
  是从只读 GPU buffer 读取。
- 第一版优先跑通 Metal，因为 golden test 和近期验证路径主要在 Metal。
- 通过 `GPUCaps::supports_readonly_storage_buffer` 控制新路径，暂时避免影响
  其他后端。
- 保持现有 `Command` 形状基本不变，为后续 transform array 和 batched stroke
  留出口。
- 尽量少引入长期概念。临时命名和兼容层可以接受，但必须便于后续删除或收敛。

## 非目标

- 第一版不把 stencil + cover 合并为 depth-test 单 pass。
- 第一版不重构全部 draw object 生命周期。
- 第一版不要求 Vulkan、GL、WebGPU 全部达到生产可用状态之后才合入 Metal
  闭环。
- 不为了一个 transform buffer 引入大型 resource binding 抽象。

## 当前流程

当前 WGX 绘制路径大致是：

1. `WGSLTessPathStrokeGeometry` 生成 WGSL。
2. `HWWGSLShaderWriter` 创建 WGX module，并创建后端 shader function。
3. WGX reflection 生成 bind group entry。
4. `PrepareCMD` 填充 reflected type definition，并调用 `UploadBindGroup`。
5. `Command::uniform_bindings` 把 buffer view 传给后端 render pass。
6. Metal render pass 通过 `setVertexBuffer` 或 `setFragmentBuffer` 绑定
   buffer。

Metal 现在已经把 uniform-like 资源翻译成 entry point 参数，例如：

```metal
constant CommonSlot& common_slot [[buffer(n)]]
```

第一版中，`var<storage, read>` 也可以走同一个 Metal buffer 绑定机制。关键
差异在 WGX reflection 语义和后续合批策略，而不是 Metal encoder API。

## 阶段一：Metal Matrix Buffer

### WGSL 形态

`WGSLTessPathStrokeGeometry` 新增 draw-data struct：

```wgsl
struct TessPathStrokeDrawData {
  transform: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> common_slot: CommonSlot;
@group(0) @binding(1) var<storage, read> draw_data: TessPathStrokeDrawData;
```

vertex position 改为：

```wgsl
let out_pos = common_slot.mvp * draw_data.transform *
              vec4<f32>(pos.xy, 0.0, 1.0);
```

对于这个 geometry，`CommonSlot.userTransform` 传 identity。`CommonSlot`
本身仍然保留 `userTransform`，因为其他 geometry 仍然在使用它。

### WGX Reflection

WGX reflection 需要区分：

- `var<uniform>` -> `BindingType::kUniformBuffer`
- `var<storage, read>` -> `BindingType::kStorageBuffer`

layout 规则：

- uniform 继续使用 std140。
- storage 对 SPIR-V/GLSL 风格后端使用 std430。
- Metal 第一版可以沿用 Metal C struct layout，但 C++ 上传结构必须和生成的
  MSL 类型匹配。

第一版 draw-data 只有一个 `mat4x4<f32>`。C++ `Matrix` 和 MSL `float4x4`
都是 64 字节，需要在 C++ 侧加 `static_assert` 兜住。

### MSL 生成

第一版只读 storage buffer 在 MSL 中生成：

```metal
constant TessPathStrokeDrawData& draw_data [[buffer(n)]]
```

这样 Metal 实现最小：

- 不需要新增 Metal resource kind。
- uniform buffer 和 read-only storage buffer 都可以用 `setVertexBuffer` /
  `setFragmentBuffer` 绑定。
- reflection 仍然保留 storage 语义，方便非 Metal 后端和未来合批基于语义做
  决策。

`UniformCapture` 这个名字长期看不准确，它实际捕获的是 bind-group resource。
第一版可以不改名，避免无关 churn。

### Runtime Binding

最小 runtime 形态：

```cpp
enum class GPUBufferBindingType {
  kUniform,
  kReadOnlyStorage,
};

struct UniformBinding {
  ...
  GPUBufferBindingType type = GPUBufferBindingType::kUniform;
};
```

`UniformBinding` 这个名字不再完全准确，但第一版可以暂时保留，减少大面积重命名。
各后端行为：

- Metal：encoder 调用可以忽略 type，仍然绑定 buffer。
- Vulkan：storage 需要使用 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`。
- GL：storage 需要绑定到 `GL_SHADER_STORAGE_BUFFER`。
- WebGPU：buffer usage 和 bind group layout 需要 storage 语义。

Metal-first patch 的成功标准：

- stroke shader 的 MSL 能编译。
- 开启 GPU tessellation 后，相关 Metal golden test 通过。
- 不支持 `supports_readonly_storage_buffer` 的后端继续走旧 `CommonSlot`
  transform 路径。
- 不引入新的 per-draw object 生命周期变化。

## 阶段二：Transform Array

单个 transform buffer 跑通以后，把 draw data 从单个 transform 扩展成数组：

```wgsl
struct TessPathStrokeDrawData {
  transforms: array<mat4x4<f32>, N>,
};
```

或者使用更适合后端的等价表示。每个 stroke item 携带一个 transform index，
shader 读取：

```wgsl
draw_data.transforms[transform_index]
```

transform index 可以来自：

- instance attribute。
- geometry stream 中稳定的 per-draw record 字段。
- 未来 indirect draw 或 draw-id 路径。

第一版合批实现应该优先选择显式 instance 数据，因为容易验证，也最容易在 Metal
上跑通。

## 阶段三：Stroke Batch Key

matrix 不再拆批以后，stroke draw 的 batch key 可以逐步收敛为：

- geometry 类型：`WGSLTessPathStrokeGeometry`
- pipeline state：blend、depth/stencil、color attachment format、sample count
- fragment program 和静态 paint pipeline
- 会影响 vertex 生成或 shader 代码的 stroke style 字段
- clip state 兼容性
- fragment 路径需要的 texture/sampler identity

动态部分追加到 buffer：

- path tessellation vertex/index payload
- per-draw transform index
- 当 paint 不属于静态 key 时的 per-draw paint data

这一步开始，`Draw`、`Geometry`、`Fragment` 应该逐步拆成静态 pipeline identity
和动态 payload upload 两部分。

## 阶段四：One-Pass Stroke

stencil + cover 会让一个逻辑 stroke 变成多个 command，不利于最终合批。transform
buffer 稳定以后，再把 stroke 路径切到 depth-test 单 pass。

目标是：可支持的情况下，一个逻辑 stroke 只产生一个可合批 command。depth state
成为 pipeline key 的一部分。不适合单 pass 的场景继续走旧 stencil/cover fallback。

## 命令顺序

合批不能破坏原始命令交错顺序。renderer 仍然以 command stream 作为顺序真相。
batch 只能合并两个 barrier 之间的一段兼容 draw item。

barrier 包括：

- saveLayer/restore 边界
- 无法放入同一批的 clip 变化
- render target 切换
- blend 或 framebuffer-fetch 依赖
- 显式 stencil/depth 依赖

在一个 run 内兼容 stroke item 可以合并；跨 barrier 时必须保持原顺序。

## 对象模型方向

长期应该拆成：

- 静态：shader source、bind group layout、pipeline key、vertex layout
- 动态：buffer range、transform index、paint data、clip/scissor、draw count

不要过早引入大型 class hierarchy。优先使用能被优化掉的小 POD descriptor 和本地
helper：

- `PipelineKey`
- `GeometryBatchKey`
- `DynamicDrawData`
- `BufferBinding`

当临时 `UniformBinding` 名字造成明显困惑时，再用一个机械 patch 改名为
`BufferBinding`。

## 包大小策略

包大小敏感时，方向应该是减少概念，而不是新增一层又一层抽象：

- 复用 WGX reflection，不额外维护后端专属 shader metadata。
- Metal storage support 优先作为现有 buffer binding 上的语义标记。
- 避免为每个 batch component 添加 virtual interface。
- 不到真正需要时，不引入持久化 `DrawItem` 抽象。
- Metal 和至少一个其他后端稳定后，删除临时兼容路径。
- 优先使用一个通用 buffer upload helper，而不是拆出 uniform/storage 两套 helper。

期望终态更接近：

```text
PipelineKey + DynamicDrawData + CommandRun
```

而不是多个 per-draw `Draw`、`Geometry`、`Fragment` 和 pipeline builder 对象都
持有重叠状态。

## 验证计划

Metal first：

1. 跑 `WGSLTessPathStrokeGeometry` 的 shader generation unit test。
2. 编译 Metal backend。
3. 跑包含 GPU tessellation 的 golden shape test：
   - `SimpleShapeGolden.*`
   - `ShapeGolden.*Stroke*`
   - `SKP_Golden.Tiger`
4. 如果 golden test 失败，先读 structured test report 和 pixel diff 文件，再改
   rendering 逻辑。

Cross-backend follow-up：

1. 保持 shader writer unit test 后端无关。
2. 增加或更新 WGX storage buffer reflection 与 MSL 输出测试。
3. GL/Vulkan/WebGPU 的 storage binding 路径实现后，再跑对应后端验证。

## 风险

- Metal `constant T&` 对第一版只读 struct 足够，但 transform array 可能需要
  `constant T*` 或 `device const T*`。
- std140/std430 和 Metal C layout 在复杂 struct 上可能分歧。新增 draw-data struct
  必须有 size/alignment 检查。
- 现有 MSL resource capture 仍然叫 `UniformCapture`，可能掩盖 storage-specific
  错误。后续 helper function 开始读取 storage data 时要特别注意。
- 临时保留 `UniformBinding` 可以降低 churn，但不应该长期制造概念混乱。

## 第一版 Checklist

- WGX reflection 支持 storage-buffer binding type。
- MSL 输出和 Metal buffer binding 能处理 `var<storage, read>`。
- `WGSLTessPathStrokeGeometry` transform 移到 `TessPathStrokeDrawData`。
- 这个 geometry 的 `CommonSlot.userTransform` 保持 identity。
- Metal golden test 通过后，再推进 transform array。
- 暂时不改变 stroke pass topology。
