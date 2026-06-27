// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/MpsEffectRenderer.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <string>

namespace {

std::string nsstring_to_string(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* text = [value UTF8String];
    return text != nullptr ? std::string(text) : std::string();
}

} // namespace

bool compile_metal_shaders(bool& skipped, std::string& message) {
    skipped = false;
    message.clear();

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            skipped = true;
            message = "no Metal device";
            return true;
        }

        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:px::MpsEffectRenderer::metal_shader_source()];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (library == nil) {
            message = nsstring_to_string([error localizedDescription]);
            return false;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"pixelart_effect_kernel"];
        if (function == nil) {
            message = "pixelart_effect_kernel function is missing";
            return false;
        }

        error = nil;
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (pipeline == nil) {
            message = nsstring_to_string([error localizedDescription]);
            return false;
        }
    }

    return true;
}
