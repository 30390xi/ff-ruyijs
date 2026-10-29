#ifndef _WIN32
// The display ICC comes from ColorSync. qcms, already linked from Firefox's
// tree, performs the pixel conversion.
#include "web_media/image_codec.h"

#include <CoreGraphics/CoreGraphics.h>

extern "C" bool ruyijs_image_srgb_to_profile(uint8_t*, size_t, const uint8_t*, size_t);

namespace ruyijs::web_media {
void ImageColorTransform(domain::web_media::ImageFrame* frame) {
  if (!frame || frame->pixels.empty() || frame->pixels.size() > 16u * 1024u * 1024u) return;
  CGColorSpaceRef space = CGDisplayCopyColorSpace(CGMainDisplayID());
  if (!space) return;
  CFDataRef profile = CGColorSpaceCopyICCData(space);
  CGColorSpaceRelease(space);
  if (!profile) return;
  const auto* bytes = CFDataGetBytePtr(profile);
  const CFIndex size = CFDataGetLength(profile);
  if (bytes && size > 0 && size <= 16 * 1024 * 1024)
    ruyijs_image_srgb_to_profile(frame->pixels.data(), frame->pixels.size(), bytes, size_t(size));
  CFRelease(profile);
}
}  // namespace ruyijs::web_media
#else
// Match the browser's sRGB-to-display transform using the actual system profile.
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include "web_media/image_codec.h"
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")
extern "C" bool ruyijs_image_srgb_to_profile(uint8_t*,size_t,const uint8_t*,size_t);
namespace ruyijs::web_media {
void ImageColorTransform(domain::web_media::ImageFrame* frame) {
  HDC dc=GetDC(nullptr);if(!dc)return;
  wchar_t path[32768];DWORD length=32768;
  const bool ok=GetICMProfileW(dc,&length,path)!=FALSE;ReleaseDC(nullptr,dc);
  if(!ok)return;
  std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return;
  const auto lengthBytes=input.tellg();if(lengthBytes<=0||lengthBytes>16*1024*1024)return;
  std::vector<uint8_t> profile(size_t(lengthBytes),0);input.seekg(0);
  if(!input.read(reinterpret_cast<char*>(profile.data()),profile.size()))return;
  ruyijs_image_srgb_to_profile(frame->pixels.data(),frame->pixels.size(),profile.data(),profile.size());
}
}

#endif
