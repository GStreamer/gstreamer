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

    NSString *docsPath = [paths objectAtIndex:0];
    NSMutableArray *entries;

#if ENABLE_IOS_LIBRARY

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
#endif
    /* Retrieve entries from iTunes file sharing */
    entries = [[NSMutableArray alloc] init];
    for (NSString *e in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docsPath error:nil])
    {
        [entries addObject:[NSString stringWithFormat:@"file://%@/%@", docsPath, e]];
    }
    self->mediaEntries = entries;

    /* Hardcoded list of Online media files */
    entries = [[NSMutableArray alloc] init];

    // Big Buck Bunny
    [entries addObject:@"http://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_480p_surround-fix.avi"];
    [entries addObject:@"http://mirrorblender.top-ix.org/peach/bigbuckbunny_movies/big_buck_bunny_480p_h264.mov"];
    [entries addObject:@"http://mirrorblender.top-ix.org/peach/bigbuckbunny_movies/big_buck_bunny_480p_stereo.ogg"];
    [entries addObject:@"http://mirrorblender.top-ix.org/peach/bigbuckbunny_movies/big_buck_bunny_480p_stereo.avi"];

    // Sintel
    // The ftp.nluug.nl links no longer work
    // [entries addObject:@"http://ftp.nluug.nl/ftp/graphics/blender/apricot/trailer/Sintel_Trailer1.480p.DivX_Plus_HD.mkv"];
    // [entries addObject:@"http://ftp.nluug.nl/ftp/graphics/blender/apricot/trailer/sintel_trailer-480p.mp4"];
    // [entries addObject:@"http://ftp.nluug.nl/ftp/graphics/blender/apricot/trailer/sintel_trailer-480p.ogv"];
    [entries addObject:@"http://mirrorblender.top-ix.org/movies/sintel-1024-surround.mp4"];

    // Tears of Steel
    [entries addObject:@"http://blender-mirror.kino3d.org/mango/download.blender.org/demo/movies/ToS/tears_of_steel_720p.mkv"];
    [entries addObject:@"http://blender-mirror.kino3d.org/mango/download.blender.org/demo/movies/ToS/tears_of_steel_720p.mov"];
    [entries addObject:@"http://media.xiph.org/mango/tears_of_steel_1080p.webm"];

    // Radio stations
    [entries addObject:@"http://radio.hbr1.com:19800/trance.ogg"];
    [entries addObject:@"http://radio.hbr1.com:19800/tronic.aac"];

    // Non-existing entries (to debug error reporting facilities)
    [entries addObject:@"http://non-existing.org/Non_Existing_Server"];
    [entries addObject:@"https://www.freedesktop.org/software/gstreamer-sdk/data/media/Non_Existing_File"];

    self->onlineEntries = entries;
}

@end
