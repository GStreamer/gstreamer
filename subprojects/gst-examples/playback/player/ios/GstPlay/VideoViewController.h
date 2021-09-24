#import <UIKit/UIKit.h>

@interface VideoViewController : UIViewController {
    IBOutlet UIBarButtonItem *play_button;
    IBOutlet UIBarButtonItem *pause_button;
    IBOutlet UIView *video_view;
    IBOutlet UIView *video_container_view;
    IBOutlet NSLayoutConstraint *video_width_constraint;
    IBOutlet NSLayoutConstraint *video_height_constraint;
    IBOutlet UIToolbar *toolbar;
    IBOutlet UITextField *time_label;
    IBOutlet UISlider *time_slider;
}

@property (retain,nonatomic) NSString *uri;

-(IBAction) play:(id)sender;
-(IBAction) pause:(id)sender;
-(IBAction) sliderValueChanged:(id)sender;
-(IBAction) sliderTouchDown:(id)sender;
-(IBAction) sliderTouchUp:(id)sender;

@end
