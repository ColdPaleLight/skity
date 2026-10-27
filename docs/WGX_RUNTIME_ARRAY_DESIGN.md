# WGX Runtime-Sized Array 支持设计

## 背景

当前 WGX 只支持固定长度数组，例如：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData, 1>;
```

这会直接卡住 tess stroke 合批：`draw_data`、`transforms` 这类 buffer 的长度应该由
batch builder 在运行时决定，而不是写死在 shader 里。现在为了让 Metal 路径先跑通，
我们临时使用长度 1 的 fixed array。下一步需要把它改成：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData>;
@group(0) @binding(3) var<storage, read> transforms: array<mat4x4<f32>>;
```

WGSL 标准允许 runtime-sized array 作为 storage buffer 的 store type，或者作为
storage buffer store struct 的最后一个成员。数组元素数量由绑定的 buffer size 决定。
这个限制很适合我们当前的 draw data / transform array 场景，因为它们本来就是只读
storage buffer。

## 目标

- WGX 能解析、验证、反射和生成 `array<T>`。
- 第一版优先支持 `var<storage, read> name: array<T>` 这种 top-level runtime array。
- CPU 侧 `TypeDefinition` 能接受运行时长度的数据，并正确计算上传 size / stride。
- Metal、Vulkan、GL、WebGPU 后端都能生成可编译、语义一致的 shader。
- 现有 `array<T, N>` 行为保持不变，gradient 等固定数组不受影响。

## 非目标

- 第一版不支持 uniform buffer 中的 runtime-sized array。
- 第一版不支持 function/private/workgroup 变量使用 runtime-sized array。
- 第一版不优先支持 `struct { header: Header, items: array<T> }`。这类 trailing
  runtime array 可以作为第二阶段，因为 MSL 和 SPIR-V 都需要更多 lowering 规则。
- 第一版不支持 runtime array 构造，也不允许把 runtime array 当普通 value 传参。

## 当前限制

WGX 当前有三个关键限制：

1. Semantic resolver 要求 `array` 必须有两个模板参数，并要求第二个参数是正整数。
2. IR `TypeKind::kArray` 使用 `count` 表示固定长度，`TypeTable::GetArrayType` 禁止
   `count == 0`。
3. CPU reflection 的 `CommonArray` 会根据固定 `count` 预创建 N 个
   `TypeDefinition`，`SetData` 也要求传入 size 必须等于 `stride * count`。

因此支持 runtime array 不能只改 printer。必须让 semantic、IR、reflection 和后端
共同理解“这个数组没有编译期长度，但有元素布局和运行时上传长度”。

## WGSL 语义约束

第一版采用以下规则：

- `array<T, N>` 仍然表示 fixed-size array。
- `array<T>` 表示 runtime-sized array。
- runtime-sized array 只能出现在 `var<storage>` binding 的 store type 上。
- `array<T>` 的 `T` 必须是可 host-shareable 的类型；第一版复用当前 WGX 已支持的
  scalar/vector/matrix/struct 类型集合。
- runtime array 可以被 index accessor 读取，例如 `draw_data[draw_id]`。
- 不要求第一版支持 `arrayLength(&draw_data)`，合批路径会在 CPU 侧保证 index 合法。

第二阶段再支持：

```wgsl
struct DrawBuffer {
  header: DrawHeader,
  items: array<DrawData>,
};
@group(0) @binding(0) var<storage, read> draws: DrawBuffer;
```

这个形态需要验证 runtime array 必须是 struct 最后一个成员，并且该 struct 只能作为
storage buffer store type 使用。

## AST 与 Semantic

AST 已经有 `ast::Array::size = nullptr` 的表达能力，因此 parser 层原则上只需要允许
`array` 的模板参数数量为 1。

需要改动：

- `Resolver::ValidateArrayType` 接受 1 或 2 个模板参数。
- 当参数数量为 1 时，标记为 runtime array，并验证 element type。
- 增加上下文验证：runtime array 只能用于 storage binding。
- 如果后续支持 trailing struct runtime array，则在 `Visit(TypeDecl/StructMember)` 中记录
  runtime array member，并验证它是最后一个成员。

建议新增 helper：

```cpp
bool IsRuntimeArrayType(const ast::Type& type);
bool IsStorageBufferVariable(const ast::Variable* variable);
```

这样可以避免把 “array 参数个数” 的判断散落在 resolver、lowerer 和 reflection 中。

## IR 表达

推荐在 IR 中显式表达 runtime array，不要用 magic count 含义散落各处。

可选方案：

1. 继续使用 `TypeKind::kArray`，并约定 `count == 0` 表示 runtime array。
2. 新增 `TypeKind::kRuntimeArray`。
3. 在 `Type` 中把 `count` 改成 `std::optional<uint32_t>`。

建议第一版采用方案 1，原因是改动最小，且当前 fixed array 已经禁止 `count == 0`。
为了避免可读性差，增加常量：

```cpp
constexpr uint32_t kRuntimeArrayCount = 0;
```

新增 API：

```cpp
TypeId GetRuntimeArrayType(TypeId element_type);
bool IsRuntimeArrayType(TypeId id) const;
```

`Lowerer::ResolveType` 遇到 `array<T>` 时调用 `GetRuntimeArrayType`。所有需要固定长度的
路径继续检查 `count != kRuntimeArrayCount`。

## CPU Reflection 与上传

当前 `CommonArray` 适合 fixed array，但不适合 runtime array，因为它预先创建所有元素。
建议新增 `RuntimeArrayDefinition`：

```cpp
struct RuntimeArrayDefinition : public ArrayDefinition {
  RuntimeArrayDefinition(std::unique_ptr<TypeDefinition> element,
                         MemoryLayout layout);

  bool SetData(const void* data, size_t size) override;
  void WriteToBuffer(void* buffer, size_t offset) const override;
  TypeDefinition* GetElementAt(uint32_t index) override;

 private:
  std::unique_ptr<TypeDefinition> element_;
  std::vector<uint8_t> raw_data_;
  size_t size_of_element_ = 0;
  size_t stride_of_element_ = 0;
};
```

行为：

- 构造时只计算 element layout、stride、alignment，`count = 0`，`size = 0`。
- `SetData(data, size)` 接受任意 `size`，但要求 `size % stride == 0`。
- `SetData` 成功后更新 `count = size / stride`、`this->size = size`，并保存 raw bytes。
- `WriteToBuffer` 直接拷贝 `raw_data_`。
- `GetElementAt` 第一版可以只返回 `element_.get()` 或返回 `nullptr`；合批路径应优先使用
  `SetData` 一次性上传，避免 per-index reflection 开销。

`UploadBindGroup` 当前使用 `entry->type_definition->size` 分配 stage buffer。这个模型可以
继续沿用，但调用方必须先 `SetData`，再 `UploadBindGroup`。如果 `size == 0`，上传函数应该
直接返回失败或跳过，避免绑定空 buffer。

## Shader 书写约定

第一版推荐所有需要变长的数据都拆成独立 storage binding：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData>;
@group(0) @binding(3) var<storage, read> transforms: array<mat4x4<f32>>;
```

这样比 `struct + trailing array` 更容易跨后端：

- Metal 可以直接生成 pointer 参数。
- GLSL 可以直接生成 SSBO unsized array member。
- SPIR-V 可以把每个 binding 包装成一个 block struct。
- CPU 侧每个 binding 的 buffer size 独立，和 batch builder 的 vector 一一对应。

## Metal 后端

Metal 不需要从 API 层区分 fixed array 和 runtime array，仍然通过
`setVertexBuffer` / `setFragmentBuffer` 绑定 `MTLBuffer`。

MSL 生成建议：

```metal
vertex VSOut vs_main(...,
                     constant TessPathStrokeDrawData* draw_data [[buffer(2)]],
                     constant float4x4* transforms [[buffer(3)]]) {
  TessPathStrokeDrawData data = draw_data[draw_id];
  float4x4 transform = transforms[data.transform_id];
}
```

注意点：

- read-only storage buffer 用 `constant T*` 即可；如果后续支持 read-write storage，
  再切到 `device T*`。
- fixed array 仍然可以继续生成 `constant array<T, N>&` 或现有等价形式。
- runtime array 不生成 `array<T>` 值类型，也不生成 reference；生成 pointer 最直接。
- `struct + trailing array` 第二阶段再做。Metal 没有和 WGSL 完全同形的 flexible array
  member，届时更适合把 trailing array lowering 成 base pointer + header load helper。

## Vulkan / SPIR-V 后端

SPIR-V 对 runtime array 使用 `OpTypeRuntimeArray`。在 Vulkan SSBO 中，runtime array
通常需要放在 block struct 的最后一个 member。因此 top-level WGSL：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData>;
```

在 SPIR-V lowering 中建议内部包装为：

```text
struct DrawDataBlock {
  draw_data: RuntimeArray<TessPathStrokeDrawData>
}
```

然后：

- `var` 的 descriptor type 仍然是 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`。
- `OpDecorate Block` 标记包装 struct。
- member 0 标记 `Offset 0`。
- runtime array 标记 `ArrayStride`。
- 所有 `draw_data[i]` 的访问 lowering 为 access chain：buffer variable -> member 0 -> i。

这意味着 IR 或 SPIR-V backend 需要知道某个 global var 是 “wrapped runtime array buffer”。
当前 `GlobalVarInfo` 已经有类似 `is_wrapped_buffer` 的字段，可以沿这个方向扩展，而不是在
前端暴露额外语法。

`arrayLength` 如果未来需要支持，可在 SPIR-V 中用 `OpArrayLength`，目标是包装 struct 的
runtime array member。

## OpenGL / GLSL 后端

GLSL 需要生成 shader storage block：

```glsl
layout(std430, binding = 2) readonly buffer DrawDataBlock {
  TessPathStrokeDrawData draw_data[];
};
```

shader 里仍然使用：

```glsl
TessPathStrokeDrawData data = draw_data[draw_id];
```

运行时：

- binding target 使用 `GL_SHADER_STORAGE_BUFFER`。
- 继续使用 `glBindBufferRange`，range 来自 `GPUBufferView::range`。
- GL 后端只有在平台支持 SSBO 时才开启 `supports_readonly_storage_buffer`。
- GLES 路径需要至少 ES 3.1；如果目标平台不满足，继续走旧 uniform/fixed path。

GLSL printer 需要给匿名 block 生成稳定名字，例如 `${var_name}_Block`，避免多个 runtime
array binding 的 block 名冲突。

## WebGPU 后端

WebGPU/WGSL 是最接近源语义的后端，可以直接保留：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData>;
```

运行时要求：

- buffer usage 包含 `Storage`。
- bind group layout 使用 read-only storage buffer。
- bind group entry 的 `size` 使用实际上传 buffer 的 range。
- 如果后续使用 `arrayLength`，WGSL 可直接写 `arrayLength(&draw_data)`。

WebGPU 标准本身支持 runtime-sized array，但也要求它只能作为 storage buffer 的 whole
store type 或 storage struct 的最后成员。WGX semantic 应该提前拒绝不符合这个限制的源，
不要把错误留给浏览器/运行时。

## 与 Tess Stroke 合批的关系

runtime array 支持后，当前 tess stroke shader 可以从：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData, 1>;
@group(0) @binding(3) var<storage, read> transforms: array<mat4x4<f32>, 1>;
```

改成：

```wgsl
@group(0) @binding(2) var<storage, read> draw_data: array<TessPathStrokeDrawData>;
@group(0) @binding(3) var<storage, read> transforms: array<mat4x4<f32>>;
```

batch builder 负责：

- 收集本批次的 `TessPathStrokeDrawData` vector。
- 收集本批次的 transform vector，并给每个 draw data 写入 `transform_id`。
- 给每个 instance 写入 `draw_id`。
- 一次性上传两个 storage buffer。

shader 侧不需要知道本批次有多少 draw，只要 CPU 保证 `draw_id` 与 `transform_id` 合法。

## 实施顺序

1. Parser/semantic 支持 `array<T>`，先只允许 top-level `var<storage>`。
2. IR 增加 `GetRuntimeArrayType` 和 `IsRuntimeArrayType`。
3. CPU reflection 增加 `RuntimeArrayDefinition`，并让 `CreateTypeDefinition` 为
   `array<T>` 返回它。
4. MSL printer 支持 runtime array storage 参数生成 `constant T*`。
5. GLSL printer 支持 runtime array storage block。
6. SPIR-V backend 支持 `OpTypeRuntimeArray` 和 storage block 包装。
7. WebGPU/WGSL backend 保留 `array<T>`。
8. 把 tess stroke 的 fixed length 1 改成 runtime array，并跑 shader writer、Metal
   golden、unit test。
9. 第二阶段再考虑 `struct + trailing array` 和 `arrayLength`。

## 测试计划

- WGX semantic 单测：
  - `array<T>` in storage binding 通过。
  - `array<T>` in uniform/function/private 失败。
  - `array<T, N>` 原有行为不变。
- Shader 生成单测：
  - MSL 输出 pointer buffer 参数。
  - GLSL 输出 `readonly buffer ... data[]`。
  - WGSL 输出保留 `array<T>`。
  - SPIR-V 至少验证包含 runtime array 类型和 storage buffer descriptor。
- Skity 渲染测试：
  - `ShaderWriter.TessPathStroke*`。
  - `ShapeGolden.*Stroke*`。
  - `GradientGolden.*`，确认 gradient fixed array 没被破坏。
  - 完整 `unit`。

## 风险与取舍

- `RuntimeArrayDefinition::SetData` 直接保存 raw bytes 会绕过 element-level
  `SetData` 校验，但它更适合批量上传，也避免为每个元素创建对象。fixed array 仍保留
  原有细粒度 API。
- 用 `count == 0` 表示 runtime array 是小改动方案，需要靠 `kRuntimeArrayCount` 和
  helper 降低误用风险。
- 第一版只支持 top-level runtime array，会限制 `header + items` 同 buffer 的表达能力。
  但当前 tess stroke 合批需要的是多个独立 storage array，足够推进主线。
- OpenGL/GLES 平台差异较大，runtime array 支持应继续受 `GPUCaps` 控制。

## 参考

- WGSL 最新规范：runtime-sized array 的元素数量由绑定 buffer size 决定，并且只能作为
  storage buffer 的 whole store type 或 store struct 最后成员。
  https://www.w3.org/TR/WGSL/
- WGSL 编辑版规范同样描述了 storage buffer store type 对 runtime-sized array 的限制。
  https://gpuweb.github.io/gpuweb/wgsl/
- Vulkan 文档说明 SPIR-V `OpArrayLength` 只能用于 storage buffer，并且 runtime array
  必须是 struct 的最后成员。
  https://docs.vulkan.org/guide/latest/buffer_array_length.html

## 合入主干前的 Checklist

下面记录了将原型实现推到可合入主干所需要做的事项，按是否阻塞合入分成三档。
A 档为合入硬门槛，B 档强烈建议在合入同 PR 或紧跟随 PR 完成，C 档可以排到后续。

### A. 必须做（不做就不能合）

1. 前端 IR 修复，让 `member.offset` 在 IR 层就是真值
   - 当前 `module/wgx/lower/lower_core.cpp` 在解析 WGSL `struct` 时把
     `StructMember{type, name, 0}` 全填 0，仅靠 uniform 路径下的
     `CreateBufferCompatibleType` 兜底重算 offset。
   - 顶层为 `array<UserStruct>` 的 storage binding 会绕过该兜底，导致 IR offset 为
     0 漏到后端，触发 SPIRV-Cross/MoltenVK 报
     `Found a buffer packing case which we cannot represent in MSL`，并在原生
     Vulkan 驱动上属于 undefined behavior（多数情况是 silent miscompile）。
   - 修复方向：把 `TypeTable::GetStructType()` 改成按 WGSL 通用规则一次算齐
     `(align, size, member.offset)`；`CreateBufferCompatibleType` 退化为
     "递归触发 + uniform 增量验证"。后端 SPIR-V 那条 patch 转为
     `assert(member.offset == 期望)` 的防御。

2. uniform 增量约束的 validation pass
   - 对应 WGSL spec §13.4.4 三条：array stride ≥ 16、struct 当作成员/数组元素时
     align ≥ 16、顶层 uniform 必须是 struct。
   - 行为应当与 Dawn (Tint) / wgpu (naga) 对齐：违规直接编译失败，而不是悄悄修
     layout。
   - 该 pass 不只服务 SSBO，还会防止未来加 uniform struct 时出 silent miscompile。

3. 测试覆盖到位
   - `array<UserStruct>` 多成员（≥3 个含 vec3/u32/f32 混搭）的 storage SSBO 测试。
   - `array<mat3x3<f32>>`：mat3x3 的 column 是 vec3，验证 `MatrixStride` 取值是 16
     而不是 12。
   - `uniform` 顶层是非 struct 标量 / array of f32：期望 validation 报错。
   - `array<UserStruct>` 元素 stride 在 std140 与 std430 下各算一次，给出字节级
     offset 的 golden 表（直接 `ASSERT_EQ`，避免再次 silent breakage）。
   - GLSL 后端 + MSL 后端对相同 WGSL 输入的 layout 一致性测试（不跑 GPU，纯
     字符串/AST 比对 offset）。

4. Vulkan validation layer 干净
   - 合入前必须在 macOS / Linux 至少一处用 `VK_LAYER_KHRONOS_validation` 跑过
     `basic_example vulkan` 与 `python3 tools/test-runner.py --suite=unit`，
     确认 spec validation error/warning 清零或显式列出原因。
   - 当前我们只在 MoltenVK 上确认 pipeline 创建成功，没有排除 spec 违规
     （例如 VUID-StandaloneSpv-* 一类）。

5. MoltenVK 之外的 Vulkan 路径冒烟
   - 至少一处 native Vulkan 驱动跑 `basic_example`（Linux mesa/lavapipe，或
     `third_party/libSwAngle/lib/libvk_swiftshader.dylib`），确认 tess stroke
     视觉正确。
   - MoltenVK 对 SPIR-V 的容忍度跟原生不同，不能只测 macOS。

6. `patches/` 下的 hot patch 归并
   - `patches/wgx-spirv-fix-buffer-struct-member-offsets.patch` 是给协作方的应急
     件。主干合入时该 patch 中的修复已经在 `module/wgx/spirv/module_builder.cpp`
     里，本仓库不应再保留 patch 文件。合入前删除，或在 docs 中显式注明它仅作为
     hot-patch 历史。

### B. 强烈建议（不影响功能正确，但影响可维护性）

7. 清理 debug 临时设施
   - `src/gpu/vk/gpu_device_vk.cc` 的 `SKITY_DUMP_SPIRV` 开关：要么保留为长期
     debug 设施并写进 `docs/BUILD_AND_RUN.md` / Vulkan 章节，要么删除。
   - `test/ut/utils/arena_allocator_test.cc` 中两个 `DISABLED_*` death test：
     要么换 `EXPECT_DEATH_IF_SUPPORTED` + 平台 guard 重新启用，要么删除；
     不能在 review 时让人困惑"为什么禁用"。
   - `tools/test-runner.py` 默认 `SKITY_VK_BACKEND=ON` 这件事要写进文档，
     避免其他贡献者因为编译时间变长而困惑。
   - `example/common/CMakeLists.txt` 里 force `VOLK_PULL_IN_VULKAN=ON` 与
     `VULKAN_HEADERS_INSTALL_DIR=third_party/Vulkan-Headers` 的 cache 写法，
     需要一段注释解释"为什么主动屏蔽宿主 Vulkan SDK"，避免装了 Vulkan SDK
     的同事疑惑。

8. HWStageBuffer 对齐改动的回归
   - `src/gpu/vk/gpu_device_vk.cc` 把 storage buffer alignment 改成
     `max(minUniformBufferOffsetAlignment, minStorageBufferOffsetAlignment)`。
   - 加一条 unit test 锁住该 invariant：在
     `minStorageBufferOffsetAlignment > minUniformBufferOffsetAlignment` 的
     mock caps 下，HWStageBuffer 也按更大那个对齐。

9. GL 后端 SSBO + WGSL runtime array 路径的 GL 版本兜底
   - caps 中 `supports_readonly_storage_buffer` 在 GL 上的开启条件需要再核：
     - GL ES 必须 ES 3.1+（SSBO 在 3.1 引入）；
     - 桌面 GL 需要 4.3+ 或 `GL_ARB_shader_storage_buffer_object`；
   - 不满足时要明确退回 vertex attribute 路径，并跑一次回归确保旧路径未被剪掉。

10. 设计文档收尾
    - 本文与 `HW_TESS_STROKE_BATCHING_DESIGN.md` 增加一节
      "WGSL 布局规则与 WebGPU 标准实现 (Dawn/Tint, wgpu/naga) 对照、
      uniform/storage 增量约束、`@align/@size` 的支持范围"。
    - 这次 bug 与漂移就源自缺少这部分文档。

11. PR 自审清单 + commit 拆分
    - 当前修改集中在 `wip: add tess stroke storage buffers` 一条 commit。合入主干
      前至少拆成：
      - `feat(wgx): support array<T> runtime-sized arrays in storage buffers`
      - `feat(wgx/spirv): emit Block / OpTypeRuntimeArray / member offsets`
      - `fix(wgx/spirv): compute member Offset / matrix decorations correctly`
      - `feat(gpu): storage buffer support across GL/MTL/VK`
      - `feat(render): tess path stroke batching via SSBO`
      - `test: add coverage for runtime-sized array layout`
      - `docs: layout / SSBO design notes`

### C. Nice-to-have（不卡合入，可后续 PR）

12. Vulkan golden 框架接 SwiftShader：让 `--suite=golden-shape/text` 也能跑
    Vulkan，进 CI。

13. 性能 microbench：tess stroke SSBO 路径 vs 老的 per-instance vertex attribute /
    multi-draw 路径，在 path 数量梯度上的吞吐与 stutter 对比，决定是否默认开启
    SSBO 路径。

14. 真机 (Android Adreno/Mali) 冒烟 + screenshot diff。

15. WGSL `@align` / `@size` 属性支持：Dawn 与 naga 都已支持，是 spec 一部分。
    补上后宿主端结构体可以与 shader 结构体直接对齐，省掉手写 padding，是把 wgx
    朝"真正的 WGSL 实现"再推一步。

### 推荐推进顺序

`A1 → A2 → A3 → A4 → A5 → A6 → B 整组 → C`。其中 A1 + A2 + A3 是合入主干的硬门槛，
其余可以分多个 PR 走。

## WGSL 布局规则与 WebGPU 标准实现对照（备忘）

WGSL 不区分 std140 / std430。spec §13 *Memory Layout* 只定义一套通用规则：

- `AlignOf(T)` / `SizeOf(T)` / `StrideOf(array<T,N>) = roundUp(AlignOf(T), SizeOf(T))`
- `vec3<T>` 的 `AlignOf` 是 `4·AlignOf(T)`（即 16 字节，f32 情况下），
  `SizeOf` 是 `3·SizeOf(T)`（即 12）；这一条在所有 address space 都成立，
  不区分 uniform/storage。
- `matCxR<f32>` 的 `AlignOf = AlignOf(vecR)`，`SizeOf = C * StrideOf(vecR)`。
- struct 成员 `offsetK = roundUp(AlignOf(memberK), offsetEnd(member k-1))`，
  struct 自身 `SizeOf` 再 round 到 struct `AlignOf`。

uniform 地址空间在通用规则之上额外加三条 *validation 谓词*（spec §13.4.4）：

1. 数组的 `StrideOf` 必须 ≥ 16；
2. 当一个 struct 出现在 struct 成员或数组元素位置时，其 `AlignOf` 必须 ≥ 16；
3. 顶层 uniform binding 必须包成 struct（标量 / 向量 / array 都不行）。

这些约束**只检查、不改 layout**：不通过就编译失败，让作者用 `@align(N)` /
`@size(N)` 显式补 padding，或者改字段顺序。同一个 struct 在 uniform 与 storage
两处的 `member.offset` 是完全相同的——address space 不影响布局。

### Dawn (Tint) 的处理

- 类型解析期一次性算 `(align, size, member.offset)`，与 address space 无关。
  入口在 `tint::sem::Struct` 与 `Resolver::Structure(...)`。
- uniform 增量约束作为独立 validate pass（`Validator::AddressSpaceLayout`），
  违规直接报错，不修 layout。
- SPIR-V 后端用同一份 offsets 发 `OpMemberDecorate Offset/ArrayStride/MatrixStride`，
  `Block`/`BufferBlock` 由 binding 的 storage class 决定，layout 数据是同一份。
- MSL 后端在 struct 间显式插 `_pad_NN` 字段，不依赖 SPIRV-Cross 推断 packing。
- HLSL 后端按 fxc/dxc packing 规则，必要时插 padding / `packoffset`。
- GLSL 后端（OpenGL ES 路径）写 uniform block 加 `layout(std140)`、SSBO 加
  `layout(std430)`。这两者只是把同一份 offsets 用 GLSL 的语法 token 再陈述一次：
  WGSL 通用规则 + uniform 增量约束的子集，恰好可以同时被 std140 / std430 表达。

### wgpu (naga) 的处理

- `naga::proc::Layouter` 是单一事实源，对每个 struct 顺序累加 offset，与
  storage class 无关。
- uniform 增量约束在 `naga::valid::interface::check_uniform_layout` 单独跑，
  违规返回 `LayoutError`。
- SPIR-V/MSL/HLSL 后端搬同一份 `member.offset`。
- GLSL 后端给 uniform block 写 `layout(std140, binding=...)`，给 SSBO 写
  `layout(std430, binding=...)`。

### 与 wgx 当前实现的差距

- wgx 在 `lower_core.cpp` 解析 struct 时塞了 `offset = 0`，再依赖
  `CreateBufferCompatibleType` 在 uniform/storage 路径上重算一遍。这相当于
  把"事实源"切成两份；本次 SSBO 路径让顶层 `array<UserStruct>` 绕过了重算，
  把历史 bug 暴露出来。
- 跟 Dawn / naga 对齐的方向：
  1. `TypeTable::GetStructType()` 第一次构造类型时按 WGSL 通用规则一次算齐
     `(align, size, member.offset)`。
  2. 加一个 `Validator::CheckUniformLayout(TypeId)`，只在 binding 用作 uniform
     时调用；不通过就报错而不是改 layout。
  3. 删除 `CreateBufferCompatibleType` 这种"按 storage class 重铺一遍"的逻辑。
  4. 后端只读不算：
     - SPIR-V：当前防御 patch 退化为"读取 `member.offset` → `OpMemberDecorate
       Offset`"加 `assert`。
     - MSL：在 struct 间插 `_pad` 显式字段。
     - GLSL：uniform block 写 `layout(std140)`，SSBO 写 `layout(std430)`，
       offsets 已经对齐。

这是把 wgx 拓扑收敛到 WebGPU 标准实现的形态：**前端语义层 = 权威，后端只是搬运。**
