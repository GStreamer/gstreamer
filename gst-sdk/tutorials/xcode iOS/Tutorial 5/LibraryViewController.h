#import <UIKit/UIKit.h>

@interface LibraryViewController : UITableViewController
{
    NSArray *mediaEntries;
    NSArray *onlineEntries;
}

- (IBAction)refresh:(id)sender;

@end
