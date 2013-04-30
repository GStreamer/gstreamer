#import <UIKit/UIKit.h>

#import "AppDelegate.h"
#include "gst_ios_backend.h"

int main(int argc, char *argv[])
{
    @autoreleasepool {
        gst_backend_init();
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
