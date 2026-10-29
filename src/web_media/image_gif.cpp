#ifndef _WIN32
// GIF display frames from ImageIO. CGImageSourceCreateImageAtIndex returns the
// frame ImageIO has already composited; this platform does not expose the
// sub-rectangle or the disposal method. WebKit's ImageDecoderCG reads these
// same frames. Pixels are un-premultiplied to straight BGRA.
#include "web_media/image_animation.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstring>
#include <limits>
#include <vector>

namespace ruyijs::web_media {
namespace {
bool NumberOf(CFDictionaryRef dict, CFStringRef key, double* out) {
  if (!dict) return false;
  CFTypeRef value = CFDictionaryGetValue(dict, key);
  if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) return false;
  return CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, out);
}
CFDictionaryRef GifDict(CFDictionaryRef props) {
  if (!props) return nullptr;
  CFTypeRef gif = CFDictionaryGetValue(props, kCGImagePropertyGIFDictionary);
  if (!gif || CFGetTypeID(gif) != CFDictionaryGetTypeID()) return nullptr;
  return static_cast<CFDictionaryRef>(gif);
}
bool StraightPixels(CGImageRef image, std::vector<uint8_t>* pixels, uint32_t* width, uint32_t* height) {
  const size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
  if (!w || !h || uint64_t(w) * h > domain::web_media::kImagePixelLimit) return false;
  std::vector<uint8_t> raw(w * h * 4, 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      raw.data(), w, h, 8, w * 4, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(space);
  if (!context) return false;
  CGContextTranslateCTM(context, 0, CGFloat(h));
  CGContextScaleCTM(context, 1, -1);
  CGContextDrawImage(context, CGRectMake(0, 0, CGFloat(w), CGFloat(h)), image);
  CGContextRelease(context);
  pixels->resize(raw.size());
  for (size_t i = 0; i < raw.size(); i += 4) {
    const uint32_t alpha = raw[i + 3];
    if (!alpha) {
      (*pixels)[i] = (*pixels)[i + 1] = (*pixels)[i + 2] = (*pixels)[i + 3] = 0;
      continue;
    }
    for (unsigned c = 0; c < 3; ++c)
      (*pixels)[i + c] = uint8_t((uint32_t(raw[i + c]) * 255u + alpha / 2) / alpha);
    (*pixels)[i + 3] = uint8_t(alpha);
  }
  *width = uint32_t(w);
  *height = uint32_t(h);
  return true;
}
}  // namespace

bool DecodeGifImage(const std::vector<uint8_t>& bytes, uint32_t desiredWidth, uint32_t desiredHeight,
                    domain::web_media::ImageData* out) {
  // CGImageSourceCreateImageAtIndex already returns ImageIO's display frame.
  // On this OS the GIF dictionary has delay, and does not have Left, Top, or
  // DisposalMethod. Compositing those frames again would paint them twice.
  // WebKit's ImageDecoderCG consumes the same display frames.
  if (!out || bytes.empty()) return false;
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes.data(), CFIndex(bytes.size()));
  if (!data) return false;
  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (!source) return false;
  const size_t count = CGImageSourceGetCount(source);
  CFDictionaryRef fileProps = CGImageSourceCopyProperties(source, nullptr);
  double loop = 0;
  NumberOf(GifDict(fileProps), kCGImagePropertyGIFLoopCount, &loop);
  if (!count || count > 10000) {
    if (fileProps) CFRelease(fileProps);
    CFRelease(source);
    return false;
  }
  out->frames.clear();
  out->animated = true;
  out->repetitions = loop > 0 ? loop : std::numeric_limits<double>::infinity();
  uint64_t timestamp = 0;
  for (size_t index = 0; index < count; ++index) {
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, index, nullptr);
    CFDictionaryRef frameProps = CGImageSourceCopyPropertiesAtIndex(source, index, nullptr);
    domain::web_media::ImageFrame complete;
    if (!image || !StraightPixels(image, &complete.pixels, &complete.width, &complete.height)) {
      if (image) CGImageRelease(image);
      if (frameProps) CFRelease(frameProps);
      if (fileProps) CFRelease(fileProps);
      CFRelease(source);
      return false;
    }
    CGImageRelease(image);
    double delay = 0;
    CFDictionaryRef gif = GifDict(frameProps);
    if (!NumberOf(gif, kCGImagePropertyGIFUnclampedDelayTime, &delay))
      NumberOf(gif, kCGImagePropertyGIFDelayTime, &delay);
    if (frameProps) CFRelease(frameProps);
    if (uint64_t(complete.width) * complete.height * count > domain::web_media::kImagePixelLimit) {
      if (fileProps) CFRelease(fileProps);
      CFRelease(source);
      return false;
    }
    complete.animated = true;
    complete.opaque = false;
    complete.timestamp = timestamp;
    const uint64_t micros = uint64_t(delay * 1000000.0);
    complete.duration = micros < 20000 ? 100000 : micros;
    timestamp += complete.duration;
    if (desiredWidth && desiredHeight &&
        (desiredWidth != complete.width || desiredHeight != complete.height)) {
      if (uint64_t(desiredWidth) * desiredHeight > domain::web_media::kImagePixelLimit) {
        if (fileProps) CFRelease(fileProps);
        CFRelease(source);
        return false;
      }
      std::vector<uint8_t> resized(size_t(desiredWidth) * desiredHeight * 4);
      for (uint32_t row = 0; row < desiredHeight; ++row)
        for (uint32_t col = 0; col < desiredWidth; ++col)
          std::memcpy(&resized[(size_t(row) * desiredWidth + col) * 4],
                      &complete.pixels[(size_t(uint64_t(row) * complete.height / desiredHeight) *
                                           complete.width +
                                       uint64_t(col) * complete.width / desiredWidth) *
                                      4],
                      4);
      complete.width = desiredWidth;
      complete.height = desiredHeight;
      complete.pixels = std::move(resized);
    }
    out->frames.push_back(std::move(complete));
  }
  if (fileProps) CFRelease(fileProps);
  CFRelease(source);
  return !out->frames.empty();
}
}  // namespace ruyijs::web_media
#else
// GIF frames returned by WIC are frame rectangles, so compose onto the canvas.
#include "web_media/image_wic.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ruyijs::web_media::wic {
bool GifFrames(IWICImagingFactory* factory, IWICBitmapDecoder* decoder,
               uint32_t desiredWidth, uint32_t desiredHeight,
               domain::web_media::ImageData* out) {
  UINT count = 0;
  Pointer<IWICMetadataQueryReader> metadata;
  if (FAILED(decoder->GetFrameCount(&count)) || FAILED(decoder->GetMetadataQueryReader(metadata.put()))) return false;
  const uint32_t width = Number(metadata, L"/logscrdesc/Width"), height = Number(metadata, L"/logscrdesc/Height");
  if (!width || !height || uint64_t(width) * height * count > domain::web_media::kImagePixelLimit) return false;
  out->animated = true; out->repetitions = 0;
  PROPVARIANT loop; PropVariantInit(&loop);
  if (SUCCEEDED(metadata->GetMetadataByName(L"/appext/data", &loop))) {
    if (loop.vt == (VT_VECTOR | VT_UI1) && loop.caub.cElems >= 4 && loop.caub.pElems[1] == 1) {
      const unsigned repeats = unsigned(loop.caub.pElems[2]) | unsigned(loop.caub.pElems[3]) << 8;
      out->repetitions = repeats ? repeats : std::numeric_limits<double>::infinity();
    }
    PropVariantClear(&loop);
  }
  std::vector<uint8_t> canvas(size_t(width) * height * 4, 0), previous;
  uint64_t timestamp = 0;
  for (UINT index = 0; index < count; ++index) {
    Pointer<IWICBitmapFrameDecode> frame;
    Pointer<IWICMetadataQueryReader> info;
    domain::web_media::ImageFrame patch;
    if (FAILED(decoder->GetFrame(index, frame.put())) ||
        FAILED(frame->GetMetadataQueryReader(info.put())) || !PixelFrame(factory, frame, 0, 0, &patch)) return false;
    const uint32_t x = Number(info, L"/imgdesc/Left"), y = Number(info, L"/imgdesc/Top");
    const uint32_t disposal = Number(info, L"/grctlext/Disposal");
    if (disposal == 3) previous = canvas;
    for (uint32_t row = 0; row < patch.height && uint64_t(y) + row < height; ++row)
      for (uint32_t col = 0; col < patch.width && uint64_t(x) + col < width; ++col) {
        const uint8_t* pixel = &patch.pixels[(size_t(row) * patch.width + col) * 4];
        if (pixel[3]) std::memcpy(&canvas[(size_t(y + row) * width + x + col) * 4], pixel, 4);
      }
    domain::web_media::ImageFrame complete;
    complete.width = width; complete.height = height; complete.pixels = canvas;
    complete.animated = true; complete.timestamp = timestamp;
    uint32_t delay = Number(info, L"/grctlext/Delay") * 10000;
    complete.duration = delay < 20000 ? 100000 : delay; timestamp += complete.duration;
    complete.opaque = false;  // The animation compositing surface has an alpha plane.
    if (desiredWidth && desiredHeight && (desiredWidth != width || desiredHeight != height)) {
      if (uint64_t(desiredWidth) * desiredHeight > domain::web_media::kImagePixelLimit) return false;
      std::vector<uint8_t> resized(size_t(desiredWidth) * desiredHeight * 4);
      for (uint32_t row = 0; row < desiredHeight; ++row) for (uint32_t col = 0; col < desiredWidth; ++col)
        std::memcpy(&resized[(size_t(row) * desiredWidth + col) * 4],
            &canvas[(size_t(uint64_t(row) * height / desiredHeight) * width + uint64_t(col) * width / desiredWidth) * 4], 4);
      complete.width = desiredWidth; complete.height = desiredHeight; complete.pixels = std::move(resized);
    }
    out->frames.push_back(std::move(complete));
    if (disposal == 2) {
      for (uint32_t row = 0; row < patch.height && uint64_t(y) + row < height; ++row)
        for (uint32_t col = 0; col < patch.width && uint64_t(x) + col < width; ++col)
          std::memset(&canvas[(size_t(y + row) * width + x + col) * 4], 0, 4);
    } else if (disposal == 3) canvas.swap(previous);
  }
  return true;
}
}

#endif
