// Copyright (c) 2025 The Bytedance, Inc . All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include <skity/gpu/gpu_context_mtl.h>
#include <memory>
#include <skity/codec/codec.hpp>
#include <skity/skity.hpp>

#include "test/bench/common/color_utils.hpp"
#include "test/bench/common/context_mtl_skity.h"
#include "test/bench/common/context_skity.hpp"
#include "test/bench/common/target_mtl_skity.h"

namespace skity {
namespace bench {

class MTLContextSkity : public ContextSkity {
 public:
  MTLContextSkity(std::unique_ptr<skity::GPUContext> gpu_context, id<MTLDevice> device,
                  id<MTLCommandQueue> queue);

  std::shared_ptr<Target> CreateTarget(Target::Options options) override;

  bool WriteToFile(std::shared_ptr<Target> target, std::string path) override;

 private:
  id<MTLDevice> device_;
  id<MTLCommandQueue> queue_;
};

std::shared_ptr<Context> CreateContextMTLSkity() {
  auto device = MTLCreateSystemDefaultDevice();
  auto queue = [device newCommandQueue];
  auto gpu_context = skity::MTLContextCreate(device, queue);
  gpu_context->SetEnableContourAA(false);
  return std::make_shared<MTLContextSkity>(std::move(gpu_context), device, queue);
}

MTLContextSkity::MTLContextSkity(std::unique_ptr<skity::GPUContext> gpu_context,
                                 id<MTLDevice> device, id<MTLCommandQueue> queue)
    : ContextSkity(std::move(gpu_context)), device_(device), queue_(queue) {}

std::shared_ptr<Target> MTLContextSkity::CreateTarget(Target::Options options) {
  return TargetMTLSkity::Create(gpu_context_.get(), options);
}

namespace {
void ConvertBGRAtoRGBA(void *raw_input, void *raw_output, size_t pixelCount) {
  uint8_t *input = static_cast<uint8_t *>(raw_input);
  uint8_t *output = static_cast<uint8_t *>(raw_output);
  for (size_t i = 0; i < pixelCount; ++i) {
    size_t index = i * 4;
    uint8_t b = input[index + 0];
    uint8_t g = input[index + 1];
    uint8_t r = input[index + 2];
    uint8_t a = input[index + 3];

    output[index + 0] = r;
    output[index + 1] = g;
    output[index + 2] = b;
    output[index + 3] = a;
  }
}

}  // namespace

bool MTLContextSkity::WriteToFile(std::shared_ptr<Target> target, std::string path) {
  auto *mtl_skity_target = static_cast<TargetMTLSkity *>(target.get());

  id<MTLBuffer> buffer = [device_ newBufferWithLength:target->GetWidth() * target->GetHeight() * 4
                                              options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> command_buffer = [queue_ commandBuffer];
  id<MTLBlitCommandEncoder> blit_command_encoder = [command_buffer blitCommandEncoder];
  auto dst_bytes_per_row = target->GetWidth() * 4;
  auto dst_bytes_per_image = target->GetHeight() * dst_bytes_per_row;
  [blit_command_encoder copyFromTexture:mtl_skity_target->GetTexture()
                            sourceSlice:0
                            sourceLevel:0
                           sourceOrigin:MTLOriginMake(0, 0, 0)
                             sourceSize:MTLSizeMake(target->GetWidth(), target->GetHeight(), 1)
                               toBuffer:buffer
                      destinationOffset:0
                 destinationBytesPerRow:dst_bytes_per_row
               destinationBytesPerImage:dst_bytes_per_image];
  [blit_command_encoder endEncoding];
  [command_buffer commit];
  [command_buffer waitUntilCompleted];
  auto data = skity::Data::MakeWithCopy(buffer.contents, buffer.length);

  ConvertBGRAtoRGBA(const_cast<void *>(data->RawData()), const_cast<void *>(data->RawData()),
                    data->Size() / 4);

  uint8_t *addr = reinterpret_cast<uint8_t *>(const_cast<void *>(data->RawData()));
  UnpremultiplyAlpha(addr, data->Size() / 4);

  auto pixmap = skity::Pixmap(data, target->GetWidth(), target->GetHeight(),
                              skity::AlphaType::kPremul_AlphaType,
                              skity::ColorType::kBGRA);  // TODO color type and alpha type

  auto codec = skity::Codec::MakePngCodec();

  auto encoded_data = codec->Encode(&pixmap);

  char full_file_name[128];

  snprintf(full_file_name, sizeof(full_file_name), "%s.png", path.c_str());

  encoded_data->WriteToFile(full_file_name);

  return true;
}

}  // namespace bench
}  // namespace skity
