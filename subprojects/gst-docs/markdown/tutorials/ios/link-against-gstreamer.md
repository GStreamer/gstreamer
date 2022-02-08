# iOS tutorial 1: Link against GStreamer

## Goal

![screenshot]

The first iOS tutorial is simple. The objective is to get the GStreamer
version and display it on screen. It exemplifies how to link against the
GStreamer library from Xcode using objective-C.

## Hello GStreamer!

The tutorials code are in the
[`tutorials/xcode iOS` folder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs/examples/tutorials/xcode%20iOS/).

It was created using the GStreamer Single View
Application template. The view contains only a `UILabel` that will be
used to display the GStreamer's version to the user.

## The User Interface

The UI uses storyboards and contains a single `View` with a centered
`UILabel`. The `ViewController` for the `View` links its
`label` variable to this `UILabel` as an `IBOutlet`.

**ViewController.h**

```
#import <UIKit/UIKit.h>

@interface ViewController : UIViewController {
    IBOutlet UILabel *label;
}

@property (retain,nonatomic) UILabel *label;

@end
```

## The GStreamer backend

All GStreamer-handling code is kept in a single Objective-C class called
`GStreamerBackend`. In successive tutorials it will get expanded, but,
for now, it only contains a method to retrieve the GStreamer version.

The `GStreamerBackend` is made in Objective-C so it can take care of the
few C-to-Objective-C conversions that might be necessary (like `char
*` to `NSString *`, for example). This eases the usage of this class by
the UI code, which is typically made in pure Objective-C.
`GStreamerBackend` serves exactly the same purpose as the JNI code in
the [](tutorials/android/index.md).

**GStreamerBackend.m**

```
#import "GStreamerBackend.h"

#include <gst/gst.h>

@implementation GStreamerBackend

-(NSString*) getGStreamerVersion
{
    char *version_utf8 = gst_version_string();
    NSString *version_string = [NSString stringWithUTF8String:version_utf8];
    g_free(version_utf8);
    return version_string;
}

@end
```

The `getGStreamerVersion()` method simply calls
`gst_version_string()` to obtain a string describing this version of
GStreamer. This [Modified
UTF8](http://en.wikipedia.org/wiki/UTF-8#Modified_UTF-8) string is then
converted to a `NSString *` by ` NSString:stringWithUTF8String `and
returned. Objective-C will take care of freeing the memory used by the
new `NSString *`, but we need to free the `char *` returned
by `gst_version_string()`.

## The View Controller

The view controller instantiates the GStremerBackend and asks it for the
GStreamer version to display at the label. That's it!

**ViewController.m**

```
#import "ViewController.h"
#import "GStreamerBackend.h"

@interface ViewController () {
    GStreamerBackend *gst_backend;
}

@end

@implementation ViewController

@synthesize label;

- (void)viewDidLoad
{
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    gst_backend = [[GStreamerBackend alloc] init];

    label.text = [NSString stringWithFormat:@"Welcome to %@!", [gst_backend getGStreamerVersion]];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
```

## Conclusion

This ends the first iOS tutorial. It has shown that, due to the
compatibility of C and Objective-C, adding GStreamer support to an iOS
app is as easy as it is on a Desktop application. An extra Objective-C
wrapper has been added (the `GStreamerBackend` class) for clarity, but
calls to the GStreamer framework are valid from any part of the
application code.

The following tutorials detail the few places in which care has to be
taken when developing specifically for the iOS platform.

It has been a pleasure having you here, and see you soon!

  [screenshot]: images/tutorials/ios-link-against-gstreamer-screenshot.png
