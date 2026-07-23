// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "io/ProjectIO.hpp"

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include <gifenc.h>
#include <miniz.h>
#include <stb_image.h>
#include <stb_image_write.h>

namespace px {

namespace {

// Private codec/mapping adapters used by the public ProjectIO facade below.
#include "detail/ProjectIODocumentJson.inc"
#include "detail/ProjectIORasterCodecs.inc"
#include "detail/ProjectIOModelCodecs.inc"
#include "detail/ProjectIORecoveryCodecs.inc"

} // namespace

// Public facade operations, grouped by persistence format.
#include "detail/ProjectIOProjectOperations.inc"
#include "detail/ProjectIORasterFormats.inc"
#include "detail/ProjectIOModelFormats.inc"

} // namespace px
