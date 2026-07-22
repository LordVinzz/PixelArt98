# PixelArt98 Competitive Feature Comparison

[README](../README.md) | [Features](../FEATURES.md) | [Animation](ANIMATION.md) | [License](../LICENSE)

This document records the features that are actually available in the current
PixelArt98 Qt application and compares them with Paint.NET 5.1.12 and Adobe
Photoshop desktop 27.x. The audit was last updated on 22 July 2026 and includes
work-in-progress changes in the local working tree, not only the last published
commit.

It is intentionally stricter than a roadmap. Code that exists but is not wired
into the user interface is not described as a usable feature.

## Status Key

- **Yes**: implemented and usable through the regular application workflow.
- **Partial**: usable, but substantially limited compared with the competing
  applications or with the documentation.
- **WIP**: present only in the current development tree, dependent on optional
  external components, or not yet ready for a normal release.
- **Dormant**: implementation code exists but is not connected to the normal UI.
- **No**: not implemented.

## Overall Position

PixelArt98 is currently an experimental pixel-art editor, not a general-purpose
replacement for Paint.NET or Photoshop. Its strongest differentiators are the
GraphEffect node workspace, sprite-oriented animation exports, partial Aseprite
interchange, and cuboid/model workflows. Its largest gaps are basic document
operations, text, brush quality, color management, non-destructive editing,
history accuracy, file-format coverage, and release maturity.

| Area | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Product maturity | **WIP**: version 0.1.0, no stable versioned release | **Yes**: mature stable product | **Yes**: mature professional product |
| Platforms | **Partial**: Windows, macOS, and Linux are targeted | Windows only | Windows and macOS |
| Multiple open documents | **No**: opening or creating replaces the current document | **Yes**: thumbnail tabs | **Yes**: tabbed, floating, and tiled documents |
| Commercial use | **No** without separate written permission | **Yes**, including business use | **Yes** with a valid subscription |
| Source availability | Source-available under a non-commercial license | Closed source | Closed source |
| Account required | No | No for the Classic edition | Yes for activation and subscription validation |
| Localization | **No** translation catalogs | Many built-in languages | Many built-in languages |
| Plugin ecosystem | **No** | **Yes**: effects and file-type plugins | **Yes**: UXP and third-party plugins |
| Actions, macros, or scripting | **No** | No native automation system | **Yes**: Actions, batch processing, droplets, and scripting |
| Cloud collaboration | **No** | No | **Yes**: cloud documents, sharing, and comments |
| Documentation accuracy | **WIP**: historical documents still overstate several features | Mature product documentation | Extensive official documentation and tutorials |

## Documents, Canvas, and Color

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Custom pixel dimensions | **Yes**: width and height from 1 to 32768 | **Yes** | **Yes** |
| New-document presets | **No** | **Yes** | **Yes**: print, photo, web, mobile, and custom presets |
| Physical units and resolution | **No** | **Yes** | **Yes** |
| Aspect-ratio presets or locking | **No** | **Yes** | **Yes** |
| Image resize | **No** | **Yes** with several resamplers | **Yes** with professional resampling options |
| Canvas resize | **No** | **Yes** with anchoring and fill controls | **Yes** |
| Crop tool or crop command | **No** | **Yes** | **Yes**, including perspective and content-aware crop |
| Artboards | **No** | No | **Yes** |
| Print workflow | **No** | Basic Windows printing | **Yes** with color-managed output |
| Pointer-centered zoom | **Yes** | **Yes** | **Yes** |
| Fit and actual-size views | **Yes** | **Yes** | **Yes** |
| Middle-button or Space-drag pan | **Yes** | **Yes** | **Yes** |
| Transparency checkerboard | **Yes** | **Yes** | **Yes** |
| Pixel grid | **Yes** | **Yes** | **Yes** |
| Rulers, guides, and smart snapping | **No** | Limited | **Yes** |
| Canvas rotation | **No** | No | **Yes** |
| Tile preview | **Yes** | No dedicated pixel-art preview | Pattern Preview |
| Large-image tiled rendering | **Dormant**: `GLTiledCanvasTexture` is not used by the Qt canvas | Mature multithreaded renderer | Mature tile, cache, GPU, and scratch-disk pipeline |
| Internal pixel precision | RGBA, 8 bits per channel only | 8-bit document editing with modern display color management | 8, 16, and 32 bits per channel |
| ICC color profiles | **No** | **Yes** | **Yes** |
| RGB, CMYK, Lab, grayscale, and indexed modes | **No**: RGBA only | Primarily RGB | **Yes** |
| HDR or wide-color-gamut editing | **No** | HDR/WCG display support; document editing remains limited | **Yes** |
| Soft proof and gamut warning | **No** | No | **Yes** |
| Metadata and EXIF workflow | **No** | Limited by format | **Yes** |
| Camera RAW workflow | **No** | No native RAW developer | **Yes** through Camera Raw |
| RGB, HSV, alpha, and hexadecimal color controls | **Yes** | **Yes** | **Yes** |
| Primary and secondary colors | **Yes** | **Yes** | **Yes** |
| Palette swatches | **Partial**: add, remove, and select colors | **Yes** | **Yes**: swatches and libraries |
| Palette ramp generation | **Yes** | No dedicated pixel-art ramp workflow | No dedicated pixel-art ramp workflow |
| Palette extraction and sorting | **No** in the current UI | **Yes** through palette workflows | **Yes** through indexed-color workflows and presets |
| Palette quantization and dithering | **Yes** | **Yes** | **Yes** |
| Luma and RGB histograms | **Yes** | **Yes** where relevant | **Yes**, with more analysis tools |

## Drawing Tools

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Pencil | **Yes** | **Yes** | **Yes** |
| Paintbrush | **Partial**: circular tip and size only, maximum 32 px | **Yes**: size, hardness, spacing, antialiasing, and pressure | **Yes**: professional brush engine |
| Tablet pressure | **No** | **Yes** through Windows Ink | **Yes** |
| Stroke smoothing or stabilization | **No** | **Yes** | **Yes** |
| Brush hardness | **No** | **Yes** | **Yes** |
| Brush spacing | **No** | **Yes** | **Yes** |
| Brush opacity and flow | **No** | **Yes** | **Yes** |
| Custom brush tips and presets | **No** | No native custom-brush engine; plugins may add options | **Yes** |
| Eraser | **Partial**: size only | **Yes**: size, hardness, spacing, and pressure | **Yes** |
| Straight line | **Yes**: width and 45-degree constraint | **Yes** | **Yes** |
| Editable curve tool | **No** | **Yes** | **Yes** through paths and shapes |
| Rectangle and ellipse | **Partial**: outline controls only in the UI | **Yes**: outline, fill, and styles | **Yes**: raster and vector shapes |
| Custom shapes | **No** | **Yes** | **Yes** |
| Gradient | **Partial**: one primary-to-secondary linear gradient | **Yes**: multiple shapes and transparency modes | **Yes**: editable gradients and fills |
| Paint bucket | **Yes**: tolerance and contiguous modes | **Yes** | **Yes** |
| Eyedropper | **Yes** | **Yes** | **Yes** |
| Clone stamp | **Partial**: one source point and brush size | **Yes** | **Yes**: multiple sources, alignment, transform, and overlay controls |
| Recolor tool | **No** | **Yes** | Several equivalent workflows |
| Healing, patch, and remove tools | **No** | No native healing workflow | **Yes** |
| Text tool | **No**: the current placeholder stamps the literal word `TEXT` | **Yes**: real font and text controls, raster result | **Yes**: editable text layers and advanced typography |
| Pen and vector paths | **No** | No | **Yes** |
| Right-button secondary drawing color | **Yes** | **Yes** | Tool-dependent |
| Live drag previews | **Yes** for the implemented drag tools | **Yes** | **Yes** |

## Selections and Transforms

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Rectangle selection | **Yes** | **Yes** | **Yes** |
| Ellipse selection | **No** | **Yes** | **Yes** |
| Freehand lasso | **Yes** | **Yes** | **Yes** |
| Polygonal or magnetic lasso | **No** | No magnetic lasso | **Yes** |
| Magic wand | **Yes** | **Yes** | **Yes** |
| Object, subject, or quick selection | **No** | No | **Yes** |
| Replace, add, subtract, and intersect | **Yes** | **Yes** | **Yes** |
| Select all, deselect, and invert | **Yes** | **Yes** | **Yes** |
| Feather and edge refinement | **No** | Limited antialiasing controls | **Yes**: Select and Mask and related tools |
| Expand, contract, or border selection | **No** | Limited | **Yes** |
| Save selection to a channel | **No** | No | **Yes** |
| Floating selection | **Yes**: one active floating selection | **Yes** | **Yes** |
| Move selected pixels | **Partial**: translation only | **Yes**: translate, rotate, and resize | **Yes** |
| Free scale and rotate handles | **No** | **Yes** | **Yes** |
| Flip and 90/180-degree rotation | **Partial**: active-cel operations, not a complete image workflow | **Yes** | **Yes** |
| Arbitrary rotate and straighten | **Partial**: generic amount slider with limited controls | **Yes** | **Yes** |
| Perspective, skew, and distort transforms | **No** | Limited | **Yes** |
| Warp, Puppet Warp, and Liquify | **No** | No | **Yes** |
| Content-Aware Scale | **No** | No | **Yes** |

## Layers, Masks, and History

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Add, duplicate, remove, and reorder layers | **Yes** | **Yes** | **Yes** |
| Rename, visibility, and opacity | **Yes** | **Yes** | **Yes** |
| Blend modes | **Yes**: Normal, Multiply, Additive, Color Burn, Color Dodge, Reflect, Glow, Overlay, Difference, Negation, Lighten, Darken, Screen, and XOR | **Yes** | **Yes**, with a larger catalog |
| Layer groups or folders | **No** | No in the stable 5.1 series | **Yes** |
| Raster layer masks | **Partial**: reveal, hide, selection-derived, clear, and enable/disable only | No native masks | **Yes** |
| Paint or directly edit a layer mask | **No** | No | **Yes** |
| Mask inversion, overlay, or select-from-mask | **No** | No | **Yes** |
| Alpha-derived mask command | **No** in the UI | No | **Yes** |
| Vector masks | **No** | No | **Yes** |
| Clip to the layer below | **Partial** | No native clipping masks | **Yes** |
| Adjustment layers | **No** | No | **Yes** |
| Smart Objects | **No** | No | **Yes** |
| Smart Filters | **No** | No | **Yes** |
| Layer styles | **No** | No | **Yes** |
| Channels | **No** | No | **Yes** |
| Merge and flatten | **Partial**: basic operations | **Yes** | **Yes** |
| Undoable layer properties | **No**: several visibility, opacity, blend, clipping, and mask changes bypass document commands | **Yes** | **Yes** |
| Undo and redo | **Partial**: linear history capped at 128 commands | **Yes**: primarily limited by disk and memory | **Yes**: configurable states and snapshots |
| History operation list | **No**: the dock contains only Undo and Redo buttons | **Yes** | **Yes** |
| Branched history | **No**: a new operation clears the redo stack | No: linear history by design | No general branch tree; snapshots are available |
| Disk-backed large history payloads | **Yes** | **Yes** | **Yes** through scratch-disk infrastructure |
| History stored in the normal project file | **No** | No | No general persistent edit history |
| Model-operation undo | **No** | Not applicable | Not applicable to current Photoshop 3D |
| Crash recovery | **WIP**: one recovery session in the current development tree | Mature crash reporting; no persistent edit-history workflow | **Yes**: configurable automatic recovery |

## Adjustments, Effects, and AI

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Brightness and contrast | **Yes** | **Yes** | **Yes** |
| HSV or HSL | **Yes** | **Yes** | **Yes** |
| Temperature and tint | **Yes** | **Yes** | **Yes** |
| Levels | **Yes** | **Yes** | **Yes** |
| Editable curves | **Yes** | **Yes** | **Yes** |
| Tonal-range controls | **Yes**, with fewer controls | **Yes** | **Yes** |
| Auto-Level and Posterize | **Yes** | **Yes** | **Yes** |
| Destructive filter catalog | About forty artistic, blur, color, distort, noise, photo, render, and stylize effects | Broad mature catalog plus plugins | Very broad professional catalog plus plugins |
| Interactive filter preview | **Yes** | **Yes** | **Yes** |
| OpenGL effect acceleration | **Dormant**: the renderer exists but the regular effect UI does not call it | GPU acceleration for nearly all built-in effects | Broad GPU acceleration |
| Metal/MPS effect acceleration | **Partial**: macOS only, optional, and disabled by default | Not applicable | Metal acceleration on macOS where supported |
| GPU result integrated with undo | **WIP**: accepted MPS previews can replace cel pixels without a normal pixel command | **Yes** | **Yes** |
| Node-based image effects | **Yes**: GraphEffect has a large node catalog, validation, live preview, and graph save/load | No | No native node graph |
| Non-destructive graph attachment to a layer | **No**: Apply rasterizes the graph result into the active cel | Not applicable | No node graph, but Smart Filters and adjustment layers are non-destructive |
| Depth-map generation | **WIP**: ONNX Runtime or OpenCV DNN, tiling, progress, and cancellation | No | Neural Depth Blur and related workflows |
| Depth feature in normal release packages | **No**: the CI packages do not bundle a model backend or model files | Not applicable | **Yes** through the supported Adobe installation |
| Generative AI | **No** | No | **Yes**: Generative Fill, Expand, Generate Image, and related Firefly features |
| AI object selection and removal | **No** | No | **Yes** |

## Animation

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Add, duplicate, delete, and select frames | **Yes** | No animation timeline | **Yes** |
| Per-frame hold duration | **Yes** | No | **Yes** |
| Drag frame edge to change duration | **Yes**: 10 ms snapping | No | **Yes** |
| Reorder frames | **No**: the current list uses static movement | No | **Yes** |
| Timeline ruler and playhead | **No** | No | **Yes** |
| Timeline zoom and dedicated scrolling controls | **No** | No | **Yes** |
| Play, pause, and stop | **Yes** | No | **Yes** |
| Step previous and next controls | **No** in the timeline UI | No | **Yes** |
| Loop and ping-pong UI | **No**: playback mode exists in the data model but is not exposed | No | Looping and richer timeline controls |
| Onion skin | **Partial**: previous frame only at a fixed 30% opacity | No | **Yes** |
| Configurable onion range, tint, and opacity | **No** | No | **Yes** |
| Timeline keyboard shortcuts documented in [ANIMATION.md](ANIMATION.md) | **No**: most are not implemented | No | **Yes** |
| Cues or markers | **No** in the UI | No | **Yes** |
| Tweening and property keyframes | **No** | No | **Yes** |
| Video layers | **No** | No | **Yes** |
| Audio timeline | **No** | No | Limited playback support |
| Spritesheet and JSON export | **Yes** | No | No native sprite workflow |
| APNG export | **Yes** | No | No native APNG export |
| Animated GIF export | **WIP**: currently uses an external FFmpeg executable | No native animation authoring | **Yes** |
| MP4 export | **WIP**: external FFmpeg dependency | No | **Yes** |
| Aseprite import and export | **Partial**: RGBA layers, cels, palette, and tags; no groups, tilemaps, linked cels, or complete blend-mode mapping | No | No |

## Models and Game-Oriented Workflows

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Cuboid model creation | **Partial**: add, remove, select, and choose a face | No | No: Photoshop's legacy 3D feature set has been removed |
| Numeric cuboid transforms | **No** | No | No |
| Translation, rotation, and scale gizmos | **No** | No | No in current Photoshop |
| UV rectangle editing | **No** in the regular UI | No | No in current Photoshop |
| Mesh UV unwrap | **Dormant**: core helpers exist but the workflow is not exposed | No | No in current Photoshop |
| Canvas UV overlay | **Dormant** | No | No in current Photoshop |
| 3D preview | **Partial**: orbit, wheel zoom, and face selection | No | No in current Photoshop |
| 3D preview pan | **No** | No | No in current Photoshop |
| View and transform gizmos | **No** | No | No in current Photoshop |
| Venice Sunset, Kiara Dawn, and Snowy Field skyboxes | **No**: historically documented but not wired into the application | No | No |
| Import model JSON | **Yes** | No | No |
| Import STL | **Yes** | No | No |
| Import Minecraft model | **Yes** | No | No |
| Export model JSON | **Yes** | No | No |
| Export glTF | **Yes** | No | No |
| Export Three.js pack | **Yes** | No | No |
| Export STL | **Yes** | No | No |
| Export Minecraft model | **Yes** | No | No |

## File Interchange

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Native layered project | **Yes**: `.pixart` stores layers, cels, frames, masks, palette, tags, and model data | **Yes**: `.pdn` | **Yes**: PSD and PSB |
| Static raster import | PNG, JPEG, BMP, and GIF | PNG, JPEG, JPEG XL, AVIF, HEIC, WebP, JPEG XR, BMP, GIF, TGA, DDS, TIFF, and more | Very broad professional format support |
| Static raster export | PNG only | Broad format support | Broad format support |
| PSD or PSB | **No** | No native support; plugins may help | **Yes** |
| TIFF | **No** | **Yes** | **Yes** |
| WebP, AVIF, HEIF, and JPEG XL | **No** | **Yes** | Broad modern-format support |
| PDF workflow | **No** | Limited through printing or plugins | **Yes** |
| SVG workflow | **Dormant**: experimental core code exists but is not compiled or exposed | No native SVG editor | Place/import support; legacy direct SVG export was discontinued |
| Import image as a new document | **Yes** | **Yes** | **Yes** |
| Import image as a layer | **Yes**, cropped or padded at the top-left without a placement dialog | **Yes** | **Yes**, with richer placement controls |
| Export current frame as PNG | **Yes** | **Yes** | **Yes** |

## Engineering and Distribution

| Feature | PixelArt98 | Paint.NET 5.1.12 | Photoshop desktop 27.x |
|---|---|---|---|
| Native application architecture | C++23, Qt 6 Widgets, and OpenGL | .NET and Windows graphics stack | Native Adobe desktop stack |
| Local automated tests | **Yes**: 16 of 16 currently pass in a full local build | Not evaluated here | Not evaluated here |
| End-user validation depth | **WIP**: tests are mostly fast unit, integration, and offscreen UI checks | Long production history | Industrial production validation |
| Release CI builds the full registered test suite | **No**: several registered test executables are omitted from the explicit build target list | Mature release infrastructure | Mature release infrastructure |
| Depth dependencies bundled in release archives | **No** | Not applicable | **Yes** for supported Adobe features |
| Animation encoder bundled | **No**: current GIF and MP4 work depends on external FFmpeg | Not applicable | **Yes** |
| Automatic update check | **WIP** through GitHub releases | **Yes** | **Yes** through Creative Cloud |
| Stable semantic-version releases | **No**: only the `continuous` tag currently exists | **Yes** | **Yes** |

## PixelArt98's Genuine Differentiators

PixelArt98 should build around the following capabilities instead of claiming
general parity with Paint.NET or Photoshop:

- GraphEffect's visual node graph, live preview, validation, and reusable graph
  files.
- Pixel-art palettes, ramp creation, quantization, and dithering.
- Frames, APNG, spritesheet plus JSON, and partial Aseprite interchange.
- Cuboid, Minecraft, STL, glTF, and Three.js-oriented model interchange.
- Cross-platform ambitions across Windows, macOS, and Linux.
- Offline use without an account or cloud dependency.

## Highest-Priority Gaps

The most important missing work is not another effect. Product credibility
depends first on:

1. replacing the placeholder text tool with real text entry, font selection,
   sizing, alignment, and an explicit raster or editable-layer model;
2. implementing image resize, canvas resize, crop, and complete image-level
   rotate/flip operations;
3. making all layer, mask, frame, and model changes genuinely undoable;
4. replacing the misleading history dock with a real operation list, or
   removing all branch-history claims;
5. adding ellipse selection, transform handles, scale, rotate, and selection
   refinement;
6. expanding the brush engine with opacity, hardness, spacing, pressure, and
   smoothing;
7. either wiring the OpenGL tiled/effect renderers into production paths or
   removing GPU and large-image claims;
8. finishing the real animation timeline before documenting shortcuts, cues,
   drag reordering, or ping-pong controls;
9. shipping and testing the depth backend in release packages before calling it
   a distributed feature;
10. fixing the release workflow so every registered test is built and run;
11. expanding static import/export beyond the current small format set;
12. deciding whether PixelArt98 is a pixel-art application or a professional
    photo editor, then aligning the roadmap and documentation with that scope.

## Maintenance Rule

When a feature changes, update this file according to user-visible behavior.
Do not mark core helpers, experimental renderers, unused assets, data-model
fields, or tests as complete application features until a normal user can reach
and use them through a supported workflow.

Official comparison references:

- Paint.NET overview: <https://paint.net/doc/latest/>
- Paint.NET tools: <https://paint.net/doc/latest/ToolsWindow.html>
- Paint.NET image operations: <https://paint.net/doc/latest/ImageMenu.html>
- Paint.NET history: <https://paint.net/doc/latest/HistoryWindow.html>
- Paint.NET license: <https://www.getpaint.net/license.html>
- Photoshop desktop help: <https://helpx.adobe.com/photoshop/desktop.html>
- Photoshop file formats: <https://helpx.adobe.com/photoshop/desktop/save-and-export/export-files-to-different-formats/photoshop-file-formats-overview.html>
- Photoshop video and animation: <https://helpx.adobe.com/photoshop/using/video-animation-overview.html>
- Photoshop generative AI: <https://helpx.adobe.com/photoshop/desktop/generative-ai/generative-ai-features-overview.html>
