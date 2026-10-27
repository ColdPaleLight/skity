# Metal 第一版实施文档：Tess Stroke Transform Storage Buffer

## 范围

本文档只描述第一版 Metal 闭环：

- 不做合批。
- 不做 transform array。
- 不改 stencil + cover。
- 只把 `WGSLTessPathStrokeGeometry` 的单个 transform 从 `CommonSlot` 挪到
  `var<storage, read>`，并在 shader 中读取使用。

这里的“SSBO”指 WGSL/WGX 层面的 storage buffer 语义。Metal 没有 OpenGL
意义上的 SSBO 对象，第一版会把只读 storage buffer 映射为 Metal buffer entry
parameter。

## 目标结果

WGSL：

```wgsl
struct TessPathStrokeDrawData {
  transform: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> common_slot: CommonSlot;
@group(0) @binding(1) var<storage, read> draw_data: TessPathStrokeDrawData;
```

MSL：

```metal
constant CommonSlot& common_slot [[buffer(0)]],
constant TessPathStrokeDrawData& draw_data [[buffer(1)]]
```

C++ runtime：

- `common_slot` 仍然上传 `mvp` 和 `clip_depth`。
- `common_slot.userTransform` 对 stroke tess path 传 identity。
- `draw_data.transform` 上传当前 draw 的 transform。
- Metal render pass 仍然使用 `setVertexBuffer:offset:atIndex:` 绑定这两个 buffer。
- 新路径由 `GPUCaps::supports_readonly_storage_buffer` 开关控制，第一版只在 Metal
  置为 true。

## 需要改动的模块

### 1. WGX BindingType

文件：

- `module/wgx/include/wgsl_cross.h`
- `module/wgx/wgsl/wgsl_function.cpp`

新增 binding type：

```cpp
enum class BindingType {
  kUndefined,
  kUniformBuffer,
  kStorageBuffer,
  kTexture,
  kSampler,
};
```

reflection 规则：

- `var<uniform>` 生成 `kUniformBuffer`。
- `var<storage, read>` 生成 `kStorageBuffer`。
- layout 为 `kWGSL` 时：
  - uniform 使用 std140。
  - storage 使用 std430。

第一版只依赖 `TessPathStrokeDrawData` 一个 `mat4x4<f32>`，但 reflection 必须先有
storage 语义，否则后续 `UploadBindGroup` 和 backend 无法区分。

### 2. MSL Printer

文件：

- `module/wgx/msl/msl_ast_printer.cpp`
- `module/wgx/msl/uniform_capture.cpp`

第一版目标是让 `var<storage, read>` 像 bind-group buffer resource 一样进入
entry point 参数列表。

当前 MSL printer 对带 `@group/@binding` 的 `var` 会放入 `addtional_inputs_`，
并在 entry point 参数中生成 buffer 参数。需要确认两个点：

1. `var<storage, read>` 不能被 MSL printer 当成非法 global var。
2. helper function 中读取 `draw_data` 时，`UniformCapture` 能把它作为 captured
   resource 传入 helper function。

第一版 MSL 类型生成策略：

```metal
constant TessPathStrokeDrawData& draw_data [[buffer(n)]]
```

不需要为 read-only storage 单独生成 `device` address space。原因：

- 第一版只读。
- 数据很小，一个 `float4x4`。
- Metal `constant` buffer 参数足够表达只读 buffer。
- runtime 绑定方式和 uniform 相同。

后续如果 transform array 需要更自然的 pointer/index 表达，再评估：

```metal
constant TessPathStrokeDrawData* draw_data [[buffer(n)]]
```

或：

```metal
device const TessPathStrokeDrawData* draw_data [[buffer(n)]]
```

### 3. GPU Buffer Binding 语义

文件：

- `src/gpu/gpu_caps.hpp`
- `src/gpu/gpu_render_pass.hpp`
- `src/gpu/mtl/gpu_device_mtl.mm`
- `src/render/hw/draw/wgx_utils.cc`

新增后端能力：

```cpp
struct GPUCaps {
  bool supports_readonly_storage_buffer = false;
};
```

Metal device 初始化时置为 true。其他后端第一版保持 false，因此
`WGSLTessPathStrokeGeometry` 会继续生成旧 uniform transform shader。

第一版可以继续复用 `UniformBinding`，但加一个 buffer binding type：

```cpp
enum class GPUBufferBindingType {
  kUniform,
  kReadOnlyStorage,
};
```

`UploadBindGroup` 逻辑：

- 如果 entry 是 `kUniformBuffer`，上传为 `kUniform`。
- 如果 entry 是 `kStorageBuffer`，上传为 `kReadOnlyStorage`。
- 其他 resource type 继续忽略。

注意：`UniformBinding` 名字只是临时兼容。Metal 第一版不需要因此做大改名。

### 4. Stage Buffer Usage

文件：

- `src/gpu/gpu_buffer.hpp`
- `src/render/hw/hw_stage_buffer.cc`
- `src/gpu/mtl/gpu_buffer_mtl.mm`

新增 `GPUBufferUsage::kStorageBuffer`。

Metal 的 `MTLBuffer` 本身不需要像 Vulkan descriptor 一样区分 uniform/storage
usage；只要 stage buffer 是一个可绑定的 `MTLBuffer` 即可。因此 Metal 第一版中：

- usage enum 需要表达 storage 语义。
- `HWStageBuffer` 创建 GPU buffer 时带上 `kStorageBuffer`。
- `GPUBufferMTL` 可以继续使用现有 `newBufferWithLength` 路径，不需要新增 Metal
  专属 usage flag。

### 5. Metal Render Pass

文件：

- `src/gpu/mtl/gpu_render_pass_mtl.mm`

第一版 Metal runtime 不需要区分 uniform/storage 调用：

```objc
[encoder setVertexBuffer:buffer offset:offset atIndex:index];
```

`SetUniformBindings` 可以继续遍历 `command->uniform_bindings`，只要里面的
`binding.index` 已经来自 WGX MSL reflection，且 `binding.buffer` 指向正确的
stage buffer range。

可以加注释说明：

- Metal read-only storage buffer 第一版与 uniform buffer 使用相同 binding API。
- `binding.type` 保留给语义和未来扩展，不影响当前 Metal encoder 调用。

### 6. WGSLTessPathStrokeGeometry

文件：

- `src/render/hw/draw/geometry/wgsl_tess_path_stroke_geometry.cc`

新增 C++ 上传结构：

```cpp
struct DrawData {
  Matrix transform;
};

static_assert(sizeof(DrawData) == sizeof(Matrix));
```

`WriteVSUniforms` 输出：

```wgsl
struct TessPathStrokeDrawData {
  transform: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> common_slot: CommonSlot;
@group(0) @binding(1) var<storage, read> draw_data: TessPathStrokeDrawData;
```

`WriteVSMain` 中不再调用旧的：

```wgsl
get_vertex_position(pos.xy, common_slot)
```

而是显式使用 storage buffer 中的 transform：

```wgsl
var out_pos: vec4<f32> =
    common_slot.mvp * draw_data.transform * vec4<f32>(pos.xy, 0.0, 1.0);
output.pos = vec4<f32>(
    out_pos.x,
    out_pos.y,
    common_slot.extraInfo[0] * out_pos.w,
    out_pos.w);
```

`PrepareCMD` 中：

1. binding 0 仍然上传 `CommonSlot`。
2. `SetupCommonInfo` 的 transform 参数传 identity。
3. binding 1 上传 `DrawData{transform}`。

伪代码：

```cpp
auto common_slot = group->GetEntry(0);
SetupCommonInfo(common_slot, context->mvp, Matrix{}, clip_depth);
UploadBindGroup(group->group, common_slot, cmd, context);

auto draw_data_entry = group->GetEntry(1);
DrawData draw_data{transform};
draw_data_entry->type_definition->SetData(&draw_data, sizeof(DrawData));
UploadBindGroup(group->group, draw_data_entry, cmd, context);
```

这里 `Matrix{}` 需要确认在当前代码语义中是 identity。如果希望更清晰，可以使用
已有的 identity constructor/helper。

### 7. Shader Writer Test

文件：

- `test/ut/render/hw/draw/hw_wgsl_shader_writer_test.cc`

更新 `GetTessPathStrokeVS()` 的期望 WGSL：

- 新增 `TessPathStrokeDrawData`。
- 新增 `@binding(1) var<storage, read> draw_data`。
- position 计算使用 `draw_data.transform`。

这个测试只验证 WGSL writer 输出，不代表 Metal MSL 已经编译成功。因此还需要 golden
test 验证 Metal runtime。

## 数据流

第一版数据流如下：

```text
Canvas matrix
  -> WGSLTessPathStrokeGeometry::PrepareCMD(transform)
  -> DrawData{transform}
  -> HWStageBuffer allocation
  -> Command::uniform_bindings(binding=1, type=kReadOnlyStorage)
  -> GPURenderPassMTL::SetUniformBindings
  -> setVertexBuffer(... atIndex=draw_data_msl_buffer_index)
  -> MSL draw_data.transform
  -> vertex position
```

`CommonSlot` 数据流仍然存在：

```text
context->mvp + clip_depth + identity userTransform
  -> CommonSlot
  -> binding=0
  -> common_slot.mvp / common_slot.extraInfo
```

## 验证步骤

必须使用统一 test runner。

最小验证：

```bash
python3 tools/test-runner.py --suite=unit --filter="ShaderWriter.TessPathStrokeWithSolidColor"
```

Metal golden 验证：

```bash
python3 tools/test-runner.py --suite=golden-shape --filter="SimpleShapeGolden.*"
python3 tools/test-runner.py --suite=golden-shape --filter="ShapeGolden.*Stroke*"
python3 tools/test-runner.py --suite=golden-shape --filter="SKP_Golden.Tiger"
```

最后跑一次相关 unit 回归：

```bash
python3 tools/test-runner.py --suite=unit
```

如果 golden 失败，必须读取 `<agent-test-report>`，并查看报告里的
`diff_pixels_file`，确认差异位置和通道变化后再改渲染逻辑。

## 第一版验收标准

- `WGSLTessPathStrokeGeometry` 的 WGSL 不再从 `CommonSlot.userTransform` 读取
  transform。
- Metal 生成的 MSL 能编译。
- Metal golden 中 GPU tess stroke 相关 case 输出一致。
- `Command` 顺序和数量可以保持不变，不要求合批。
- 没有新增长期的大型抽象。

## 后续扩展点

第一版完成后，再进入下一步：

1. 把 `TessPathStrokeDrawData.transform` 扩为 transform array。
2. 为每个 stroke item 提供 transform index。
3. 再开始设计 stroke batch key 和 command run 合并。
4. 最后评估 stencil + cover 到 depth-test 单 pass 的替换。
