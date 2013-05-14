#import "LibraryViewController.h"
#import "VideoViewController.h"

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
    return 2;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    switch (section)
    {
        case 0: return @"Local files (iTunes file sharing)";
        default: return @"Online files";
    }
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case 0:
            return [self->mediaEntries count];
        case 1:
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

    if(indexPath.section == 0)
    {
        subtitle.text = [NSString stringWithFormat:@"file://%@",
                      [self->mediaEntries objectAtIndex:indexPath.item], nil];
    } else if (indexPath.section == 1)
    {
        subtitle.text = [self->onlineEntries objectAtIndex:indexPath.item];
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

    NSMutableArray *entries = [[NSMutableArray alloc] init];
    for (NSString *e in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docsPath error:nil])
    {
        [entries addObject:[NSString stringWithFormat:@"%@/%@",docsPath, e]];
    }
    self->mediaEntries = entries;
    self->onlineEntries = [NSArray arrayWithObjects:
        @"http://docs.gstreamer.com/media/sintel_trailer-368p.ogv",
        @"http://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_640x360.m4v",
        nil];
}

@end
