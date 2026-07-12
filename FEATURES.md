# PixelArt98 Feature Reference

[README](README.md) | [Animation](docs/ANIMATION.md) | [License](LICENSE)

This document is the behavior-parity checklist for the Qt application. A UI
change is incomplete if it removes or changes any behavior listed here.

## Documents and Interchange

- Create canvases with custom pixel dimensions, print units, resolution, presets, aspect presets, and optional aspect locking.
- Save and load native `.pixart` projects without flattening layers, frames, palettes, masks, history-independent model data, or animation metadata.
- Import raster images as a new document or as a layer.
- Export the current frame as PNG and animations as spritesheet plus JSON, GIF, or APNG.
- Import and export Aseprite documents.
- Import model JSON, STL, and Minecraft models; export model JSON, glTF, ThreeJSPack, STL, and Minecraft models.

## Canvas and Tools

- Display transparency checkerboard, pixel grid, onion skin, UV overlays, tiled previews, nearest-neighbor pixels, and large-image level-of-detail data.
- Zoom around the pointer, zoom in/out, use actual size, fit the document, and pan with the middle button or Space-drag.
- Draw with Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Bucket, Gradient, Eyedropper, Clone Stamp, Rectangle Select, Lasso Select, Magic Wand, Move Pixels, and Text.
- Configure primary/secondary colors, brush size, tolerance, contiguous matching, clone source, mask editing, and mask overlay.
- Preview drag tools live; constrain rectangles and ellipses to 1:1 and lines to 45-degree increments with Ctrl/Cmd.
- Use the secondary color/action with the right button and adjust brush size with the bracket keys.
- Replace, add, subtract, intersect, or invert selections with pointer-button and modifier combinations.
- Select all, deselect, invert, delete, nudge, float, move, commit, or cancel selected pixels.

## Layers, Masks, and Color

- Add, duplicate, remove, reorder, rename, show, or hide layers.
- Set layer opacity, clipping, masks, and Normal, Multiply, Additive, Color Burn, Color Dodge, Reflect, Glow, Overlay, Difference, Negation, Lighten, Darken, Screen, or XOR blending.
- Create reveal-all, hide-all, selection-derived, or alpha-derived masks; invert, clear, select, edit, enable, disable, and overlay masks.
- Choose primary and secondary colors, swap them, and add/remove/extract/sort palette swatches.
- Create palette ramps and remap pixels to a palette with or without dithering.

## History and Transforms

- Undo and redo document and model operations with Ctrl/Cmd shortcuts and the branched history view.
- Preserve labeled branches, preferred redo paths, bounded pruning, and lightweight disk-backed history for very large documents.
- Flip horizontally or vertically and rotate 90 degrees clockwise/counter-clockwise or 180 degrees.
- Straighten interactively and preview rotate, zoom, pan, and Nearest/Bilinear/Bicubic resampling before applying or cancelling.

## Adjustments and Effects

- Preview and apply Brightness/Contrast, HSV, Temperature, Levels, Tonal Range, editable Curves, palette quantization/dithering, Auto-Level, and Posterize.
- Inspect luma, red, green, and blue histograms, including sampled histograms for very large images.
- Artistic: Ink Sketch, Oil Painting, Pencil Sketch.
- Blurs: Depth of Field, Gaussian, Motion, Radial, Zoom, Median, Surface.
- Color: Auto-Level, Black and White, Sepia, Invert Colors, Invert Alpha, Posterize.
- Distort: Bulge, Crystalize, Dents, Frosted Glass, Pixelate, Polar Inversion, Tile Reflection, Twist.
- Noise and object: Add Noise, Median, Reduce Noise, Feather, Outline.
- Photo: Glow, Red Eye Removal, Sharpen, Soften Portrait, Vignette.
- Render and stylize: Clouds, Julia Fractal, Mandelbrot Fractal, Turbulence, Edge Detect, Emboss, Outline, Relief.
- Use OpenGL acceleration when supported, optional Metal/MPS acceleration on macOS, capability-based chunking, and CPU fallback without changing effect output semantics.
- Generate a Depth Anything V2 Small depth-map layer with source-layer, backend, tile, overlap, CPU-fallback, progress, and cancellation controls.

## Animation

- Add blank frames, duplicate, delete, select, and reorder frames; edit each frame's hold duration.
- Play, pause, stop, step, and use Loop or Ping-Pong playback with onion skinning.
- Show a time ruler and playhead, zoom or horizontally scroll the timeline, drag frames, and perform quantized, fine, or adjacent trimming.
- Add, move, and rename cues.
- Preserve the focus-scoped Space, Enter, arrows, Shift+arrows, `P`, `N`, `M`, Ctrl/Cmd+C/V, Delete, wheel, Ctrl/Cmd+wheel, and Shift+wheel timeline controls documented in [Animation](docs/ANIMATION.md).

## Models and Application

- Add, remove, select, translate, rotate, and scale cuboids and imported mesh components with axis gizmos and snapping modifiers.
- Select cuboid faces or mesh faces/vertices, edit UV rectangles, drag UVs over the texture atlas, unwrap mesh UVs, and resize the canvas when required.
- Show selected/all cuboid wireframes, transparent-face hints, canvas UV overlays, texture metadata synchronization, and cached 3D rendering.
- Orbit, pan, zoom, select faces, use view and transform gizmos, and choose the Venice Sunset, Kiara Dawn, or Snowy Field skybox.
- Persist dock visibility/layout, splash preference, automatic error-console behavior, heavy-GPU and MPS preferences, and depth settings.
- Report file, renderer, import, export, model, and worker errors in the error console; keep long imports and depth extraction cancellable and responsive.
