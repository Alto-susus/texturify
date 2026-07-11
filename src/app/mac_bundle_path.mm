#include "app/mac_bundle_path.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>

namespace app {

std::string macBundleResourcePath() {
  @autoreleasepool {
    NSString* path = [[NSBundle mainBundle] resourcePath];
    return path ? std::string(path.UTF8String) : std::string();
  }
}

} // namespace app
#endif
