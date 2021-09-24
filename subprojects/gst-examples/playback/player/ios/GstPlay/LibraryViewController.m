#import "LibraryViewController.h"
#import "VideoViewController.h"
#import <AssetsLibrary/AssetsLibrary.h>

#define ENABLE_IOS_LIBRARY false

@interface LibraryViewController ()

@end

@implementation LibraryViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    [super setTitle:@"Library"];
    [self refreshMediaItems];
}

- (IBAction)refresh:(id)sender
{
    [self refreshMediaItems];
    [self.tableView reloadData];
}

static NSString *CellIdentifier = @"CellIdentifier";

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 3;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    switch (section)
    {
        case 0: return @"Photo library";
        case 1: return @"iTunes file sharing";
        default: return @"Online files";
    }
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case 0:
            return [self->libraryEntries count];
        case 1:
            return [self->mediaEntries count];
        case 2:
            return [self->onlineEntries count];
        default:
            return 0;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier];
    // Configure Cell
    UILabel *title = (UILabel *)[cell.contentView viewWithTag:10];
    UILabel *subtitle = (UILabel *)[cell.contentView viewWithTag:11];

    switch (indexPath.section) {
        case 0: subtitle.text = [self->libraryEntries objectAtIndex:indexPath.item];
            break;
        case 1: subtitle.text = [self->mediaEntries objectAtIndex:indexPath.item];
            break;
        case 2: subtitle.text = [self->onlineEntries objectAtIndex:indexPath.item];
            break;
        default:
            break;
    }

    NSArray *components = [subtitle.text pathComponents];
    title.text = components.lastObject;

    return cell;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath {
    return NO;
}

- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)indexPath {
    return NO;
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    if ([segue.identifier isEqualToString:@"playVideo"]) {
        NSIndexPath *indexPath = [self.tableView indexPathForSelectedRow];
        VideoViewController *destViewController = segue.destinationViewController;
        UITableViewCell *cell = [[self tableView] cellForRowAtIndexPath:indexPath];
        UILabel *label = (UILabel *)[cell.contentView viewWithTag:10];
        destViewController.title = label.text;
        label = (UILabel *)[cell.contentView viewWithTag:11];
        destViewController.uri = label.text;
    }
}

- (void)refreshMediaItems {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSAllDomainsMask, YES);

#if ENABLE_IOS_LIBRARY

    NSString *docsPath = [paths objectAtIndex:0];
    NSMutableArray *entries;

    /* Entries from the Photo Library */
    entries = [[NSMutableArray alloc] init];
    ALAssetsLibrary *library = [[ALAssetsLibrary alloc] init];
    [library enumerateGroupsWithTypes:ALAssetsGroupAll
        usingBlock:^(ALAssetsGroup *group, BOOL *stop)
        {
            if (group) {
                [group enumerateAssetsUsingBlock:^(ALAsset *result, NSUInteger index, BOOL *stop)
                {
                    if(result) {
                        [entries addObject:[NSString stringWithFormat:@"%@",[result valueForProperty:ALAssetPropertyAssetURL]]];
                        *stop = NO;

                    }
                }];
            } else {
                [self.tableView reloadData];
            }
        }
        failureBlock:^(NSError *error)
        {
            NSLog(@"ERROR");
        }
     ];
    self->libraryEntries = entries;

    /* Retrieve entries from iTunes file sharing */
    entries = [[NSMutableArray alloc] init];
    for (NSString *e in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docsPath error:nil])
    {
        [entries addObject:[NSString stringWithFormat:@"file://%@/%@", docsPath, e]];
    }
    self->mediaEntries = entries;
#endif

    self->onlineEntries = [NSArray arrayWithContentsOfURL:[[NSBundle mainBundle] URLForResource:@"OnlineMedia" withExtension:@"plist"]];
}

@end
