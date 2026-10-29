#ifndef _WIN32
// Animation patches through ImageIO. Pixel coverage matches the Windows
// compositor; only the frame source differs.
#include "image_animation.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace ruyijs::web_media {
namespace {
bool DecodeStraight(const std::vector<uint8_t>& encoded, domain::web_media::ImageFrame* out) {
  if (encoded.empty() || !out) return false;
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, encoded.data(), CFIndex(encoded.size()));
  if (!data) return false;
  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (!source) return false;
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (!image) return false;
  const size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
  if (!w || !h || uint64_t(w) * h > domain::web_media::kImagePixelLimit) {
    CGImageRelease(image);
    return false;
  }
  std::vector<uint8_t> raw(w * h * 4, 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      raw.data(), w, h, 8, w * 4, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(space);
  if (!context) {
    CGImageRelease(image);
    return false;
  }
  CGContextTranslateCTM(context, 0, CGFloat(h));
  CGContextScaleCTM(context, 1, -1);
  CGContextDrawImage(context, CGRectMake(0, 0, CGFloat(w), CGFloat(h)), image);
  CGContextRelease(context);
  CGImageRelease(image);
  out->width = uint32_t(w);
  out->height = uint32_t(h);
  out->pixels.resize(raw.size());
  out->opaque = true;
  for (size_t i = 0; i < raw.size(); i += 4) {
    const uint32_t alpha = raw[i + 3];
    if (alpha != 255) out->opaque = false;
    if (!alpha) {
      out->pixels[i] = out->pixels[i + 1] = out->pixels[i + 2] = out->pixels[i + 3] = 0;
      continue;
    }
    for (unsigned c = 0; c < 3; ++c)
      out->pixels[i + c] = uint8_t((uint32_t(raw[i + c]) * 255u + alpha / 2) / alpha);
    out->pixels[i + 3] = uint8_t(alpha);
  }
  return true;
}
bool ScaleFrame(uint32_t width, uint32_t height, domain::web_media::ImageFrame* frame) {
  if (!width || !height || (width == frame->width && height == frame->height)) return true;
  if (uint64_t(width) * height > domain::web_media::kImagePixelLimit) return false;
  // The canvas is already premultiplied BGRA, which is what WIC scales as
  // 32bppPBGRA. Leave that alpha mode in place after the high-quality scale.
  std::vector<uint8_t> scaled(size_t(width) * height * 4, 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(scaled.data(), width, height, 8, width * 4, space,
                                               kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(space);
  if (!context) return false;
  CGDataProviderRef provider =
      CGDataProviderCreateWithData(nullptr, frame->pixels.data(), frame->pixels.size(), nullptr);
  CGColorSpaceRef sourceSpace = CGColorSpaceCreateDeviceRGB();
  CGImageRef image = CGImageCreate(frame->width, frame->height, 8, 32, frame->width * 4, sourceSpace,
                                   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider,
                                   nullptr, false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(sourceSpace);
  CGDataProviderRelease(provider);
  if (!image) {
    CGContextRelease(context);
    return false;
  }
  CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
  CGContextTranslateCTM(context, 0, CGFloat(height));
  CGContextScaleCTM(context, 1, -1);
  CGContextDrawImage(context, CGRectMake(0, 0, CGFloat(width), CGFloat(height)), image);
  CGImageRelease(image);
  CGContextRelease(context);
  frame->width = width;
  frame->height = height;
  frame->pixels = std::move(scaled);
  return true;
}
}  // namespace

bool ComposeImageAnimation(const std::string& mime, const ImageAnimation& input, uint32_t width,
                           uint32_t height, domain::web_media::ImageData* out) {
  if (!input.width || !input.height || !out) return false;
  if (uint64_t(input.width) * input.height > domain::web_media::kImagePixelLimit) return false;
  std::vector<uint8_t> canvas(size_t(input.width) * input.height * 4, 0), previous;
  uint64_t timestamp = 0;
  bool alphaSeen = false;
  out->animated = true;
  out->repetitions = input.repetitions;
  for (const auto& patch : input.patches) {
    domain::web_media::ImageFrame image;
    if (!DecodeStraight(patch.encoded, &image)) return !input.complete;
    if (image.width != patch.width || image.height != patch.height) return false;
    if (uint64_t(patch.x) + patch.width > input.width || uint64_t(patch.y) + patch.height > input.height)
      return false;
    if (patch.disposal == 2) previous = canvas;
    for (uint32_t y = 0; y < image.height; ++y)
      for (uint32_t x = 0; x < image.width; ++x) {
        const auto* source = &image.pixels[(size_t(y) * image.width + x) * 4];
        auto* target = &canvas[(size_t(y + patch.y) * input.width + x + patch.x) * 4];
        const uint32_t alpha = source[3];
        for (unsigned c = 0; c < 4; ++c) {
          const uint32_t value = c == 3 ? alpha : (uint32_t(source[c]) * alpha + 254) / 255;
          target[c] = uint8_t(patch.over ? std::min(255u, value + (uint32_t(target[c]) * (255 - alpha) + 127) / 255)
                                        : value);
        }
      }
    domain::web_media::ImageFrame complete;
    complete.width = input.width;
    complete.height = input.height;
    if (!image.opaque) alphaSeen = true;
    complete.pixels = canvas;
    complete.animated = true;
    complete.opaque = mime == "image/webp" && !alphaSeen;
    if (complete.opaque)
      for (size_t i = 3; i < canvas.size(); i += 4)
        if (canvas[i] != 255) {
          complete.opaque = false;
          break;
        }
    complete.timestamp = timestamp;
    complete.duration = patch.duration;
    timestamp += patch.duration;
    if (!ScaleFrame(width, height, &complete)) return false;
    out->frames.push_back(std::move(complete));
    if (patch.disposal == 1) {
      for (uint32_t y = 0; y < patch.height; ++y)
        std::memset(&canvas[(size_t(y + patch.y) * input.width + patch.x) * 4], 0, size_t(patch.width) * 4);
    } else if (patch.disposal == 2) {
      canvas.swap(previous);
    }
  }
  return true;
}
}  // namespace ruyijs::web_media
#else
// Shared WebMedia canvas composition; the codec supplies actual frame pixels.
#include "image_animation.h"
#include <algorithm>
#include <cstring>
namespace ruyijs::web_media {
namespace {
bool Pixels(IWICImagingFactory* factory,const std::string& mime,const ImagePatch& patch,domain::web_media::ImageFrame* out) {
  wic::Pointer<IWICStream> stream;wic::Pointer<IWICBitmapDecoder> decoder;wic::Pointer<IWICBitmapFrameDecode> frame;
  if(FAILED(factory->CreateStream(stream.put()))||FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(patch.encoded.data()),DWORD(patch.encoded.size())))||
      FAILED(factory->CreateDecoder(*wic::Container(mime),nullptr,decoder.put()))||FAILED(decoder->Initialize(stream,WICDecodeMetadataCacheOnLoad))||
      FAILED(decoder->GetFrame(0,frame.put())))return false;
  return wic::PixelFrame(factory,frame,0,0,out);
}
bool Resize(IWICImagingFactory* factory,uint32_t w,uint32_t h,domain::web_media::ImageFrame* frame) {
  if(!w||!h||(w==frame->width&&h==frame->height))return true;
  if(uint64_t(w)*h>domain::web_media::kImagePixelLimit)return false;
  wic::Pointer<IWICBitmap> bitmap;wic::Pointer<IWICBitmapScaler> scaler;
  if(FAILED(factory->CreateBitmapFromMemory(frame->width,frame->height,GUID_WICPixelFormat32bppPBGRA,
      frame->width*4,UINT(frame->pixels.size()),frame->pixels.data(),bitmap.put()))||FAILED(factory->CreateBitmapScaler(scaler.put()))||
      FAILED(scaler->Initialize(bitmap,w,h,WICBitmapInterpolationModeFant)))return false;
  std::vector<uint8_t> pixels(size_t(w)*h*4);
  if(FAILED(scaler->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data())))return false;
  frame->width=w;frame->height=h;frame->pixels=std::move(pixels);return true;
}
}
bool ComposeImageAnimation(const std::string& mime,const ImageAnimation& input,uint32_t w,uint32_t h,domain::web_media::ImageData* out) {
  if(!input.width||!input.height)return false;
  wic::Apartment apartment;wic::Pointer<IWICImagingFactory> factory;
  if(!apartment.ok()||FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put()))))return false;
  std::vector<uint8_t> canvas(size_t(input.width)*input.height*4,0),previous;uint64_t timestamp=0;bool alphaSeen=false;
  out->animated=true;out->repetitions=input.repetitions;
  for(const auto& patch:input.patches) {
    domain::web_media::ImageFrame image;if(!Pixels(factory,mime,patch,&image))return !input.complete;
    if(image.width!=patch.width||image.height!=patch.height)return false;
    if(patch.disposal==2)previous=canvas;
    for(uint32_t y=0;y<image.height;y++)for(uint32_t x=0;x<image.width;x++) {
      const auto* source=&image.pixels[(size_t(y)*image.width+x)*4];auto* target=&canvas[(size_t(y+patch.y)*input.width+x+patch.x)*4];
      const uint32_t alpha=source[3];
      for(unsigned c=0;c<4;c++) {
        const uint32_t value=c==3?alpha:(uint32_t(source[c])*alpha+254)/255;
        target[c]=uint8_t(patch.over?std::min(255u,value+(uint32_t(target[c])*(255-alpha)+127)/255):value);
      }
    }
    domain::web_media::ImageFrame complete;complete.width=input.width;complete.height=input.height;
    if(!image.opaque)alphaSeen=true;
    complete.pixels=canvas;complete.animated=true;complete.opaque=mime=="image/webp"&&!alphaSeen;
    if(complete.opaque)for(size_t i=3;i<canvas.size();i+=4)if(canvas[i]!=255){complete.opaque=false;break;}
    complete.timestamp=timestamp;complete.duration=patch.duration;timestamp+=patch.duration;
    if(!Resize(factory,w,h,&complete))return false;out->frames.push_back(std::move(complete));
    if(patch.disposal==1)for(uint32_t y=0;y<patch.height;y++)
      std::memset(&canvas[(size_t(y+patch.y)*input.width+patch.x)*4],0,size_t(patch.width)*4);
    else if(patch.disposal==2)canvas.swap(previous);
  }
  return true;
}
}
#endif
