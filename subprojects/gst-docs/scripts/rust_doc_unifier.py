#!/usr/bin/env python3

import os
import sys
import json
import html
import shutil
from collections import OrderedDict
from pathlib import Path
from bs4 import BeautifulSoup
import zipfile
from urllib.request import urlretrieve
from multiprocessing import Pool, cpu_count, Manager
from functools import partial
import traceback
import gitlab


def get_documentation_artifact_url(project_name='gstreamer/gstreamer',
                                   job_name='documentation',
                                   branch='main') -> str:
    """
    Returns the URL of the latest artifact from GitLab for the specified job.

    Args:
        project_name (str): Name of the GitLab project
        job_name (str): Name of the job
        branch (str): Name of the git branch
    """
    gl = gitlab.Gitlab("https://gitlab.freedesktop.org/")
    project = gl.projects.get(project_name)
    pipelines = project.pipelines.list(get_all=False)
    for pipeline in pipelines:
        if pipeline.ref != branch:
            continue

        job, = [j for j in pipeline.jobs.list(iterator=True)
            if j.name == job_name]
        if job.status != "success":
            continue

        return f"https://gitlab.freedesktop.org/{project_name}/-/jobs/{job.id}/artifacts/download"

    raise Exception("Could not find documentation artifact")


def get_relative_prefix(file_path, docs_root):
    """
    Returns the relative path prefix for a given HTML file.

    Args:
        file_path (Path): Path to the HTML file
        docs_root (Path): Root directory of the documentation
    """
    rel_path = os.path.relpath(docs_root, file_path.parent)
    if rel_path == '.':
        return './'
    return '../' + '../' * rel_path.count(os.sep)


def fix_relative_urls(element, prefix):
    """
    Fixes relative URLs in a hotdoc component to include the correct prefix.

    Args:
        element: BeautifulSoup element containing hotdoc navigation or resources
        prefix: Prefix to add to relative URLs
    """
    # Fix href attributes
    for tag in element.find_all(True, {'href': True}):
        url = tag['href']
        if url.startswith(('http://', 'https://', 'mailto:', '#', 'javascript:')):
            continue

        if url.endswith('/') or '.' not in url.split('/')[-1]:
            if not url.endswith('index.html'):
                url = url.rstrip('/') + '/index.html'

        if ".html" in url and '?gi-language=' not in url:
            url += '?gi-language=rust'

        tag['href'] = prefix + url

    # Fix src attributes
    for tag in element.find_all(True, {'src': True}):
        url = tag['src']
        if not url.startswith(('http://', 'https://', 'data:', 'javascript:')):
            if '?gi-language=' not in url:
                url += '?gi-language=rust'
            tag['src'] = prefix + url


def extract_hotdoc_resources(index_html_soup, prefix):
    """
    Extracts required CSS and JS resources from the main hotdoc page.
    Returns tuple of (css_links, js_scripts)
    """
    head = index_html_soup.find('head')

    # Extract CSS links
    css_links = [link for link in head.find_all('link') if 'enable_search.css'
                 not in link['href']]

    # Extract JS scripts
    js_scripts = []
    for script in head.find_all('script'):
        src = script.get('src', '')
        if [unwanted for unwanted in ["trie_index.js", "prism-console-min.js", 'trie.js', 'language-menu.js'] if unwanted in src]:
            continue

        if 'language_switching.js' in script['src']:
            js_scripts.append(BeautifulSoup('<script src="assets/js/utils.js" />', 'html.parser'))

            # Inject necessary data for the 'language_switching.js' script to
            # properly populate the Language dropdown menu for us.
            js_scripts.append(BeautifulSoup(f'''
<script>
    utils.hd_context.project_url_path = "/../{prefix}libs.html";
    utils.hd_context.gi_languages = ['c', 'python', 'javascript', 'rust'];
</script>
''', 'html.parser'))

        js_scripts.append(script)

    return css_links, js_scripts


def extract_hotdoc_nav(index_html_soup):
    """
    Extracts the navigation bar from the main GStreamer page.
    Returns the navigation HTML.
    """
    nav = index_html_soup.find('nav', class_='navbar')

    for tag in nav.find_all(True, {'href': True}):
        url = tag['href']
        if "gstreamer/gi-index.html" in url:
            tag['href'] = "rust/stable/latest/docs/gstreamer/index.html"
        elif "libs.html" in url:
            tag['href'] = "rust/stable/latest/docs/index.html"

    return nav


def get_hotdoc_components(docs_root, prefix):
    """
    Reads the main GStreamer page and extracts required components.

    Returns tuple of (resources_html, nav_html)
    """
    index_path = docs_root / "index.html"
    with open(index_path, 'r', encoding='utf-8') as f:
        content = f.read()

    soup = BeautifulSoup(content, 'html.parser')

    # Extract resources and navigation first
    css_links, js_scripts = extract_hotdoc_resources(soup, prefix)
    nav = extract_hotdoc_nav(soup)
    if not css_links:
        raise Exception("Failed to extract CSS links")
    if not js_scripts:
        raise Exception("Failed to extract JS scripts")
    if not nav:
        raise Exception("Failed to extract navigation")

    resources_soup = BeautifulSoup("<div></div>", 'html.parser')
    assert resources_soup.div
    for component in css_links + js_scripts:
        resources_soup.div.append(component)

    # Fix URLs in the extracted components
    fix_relative_urls(resources_soup, prefix)
    fix_relative_urls(nav, prefix)

    # Build final HTML
    resources_html = "\n".join(str(tag) for tag in resources_soup.div.contents)
    resources_html += f'\n<script src="{prefix}assets/rustdoc/js/theme-sync.js" />'
    nav_html = str(nav) if nav else ""

    return resources_html, nav_html


def modify_rustdoc_html_file(file_path, docs_root):
    """Modifies a single HTML file to include hotdoc navigation."""
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Calculate the relative path prefix for this file
    prefix = get_relative_prefix(file_path, docs_root)

    # Get hotdoc components with fixed URLs
    resources_html, nav_html = get_hotdoc_components(docs_root, prefix)

    soup = BeautifulSoup(content, 'html.parser')

    remove_rustdoc_settings(soup)
    if not add_rust_api_menu(soup, prefix):
        raise Exception("Failed to add Rust API menu")

    # Add hotdoc resources to head
    head = soup.find('head')
    if not head or not resources_html:
        raise Exception("Failed to add get hotdoc components")

    rust_versions = OrderedDict({
        "Stable": f'{prefix}rust/stable/latest/docs/index.html',
        "Development": f'{prefix}rust/git/docs/index.html',
    })

    for file in (docs_root / "rust" / "stable").iterdir():
        if file.name == "latest":
            continue

        if file.is_dir() and (file / "index.html").exists():
            rust_versions[file.name] = f'{prefix}rust/stable/{file.name}/docs/index.html'

    head.insert(0, BeautifulSoup(
        f'''<meta id="hotdoc-rust-info"
                hotdoc-root-prefix="{prefix}"
                hotdoc-rustdoc-versions="{html.escape(json.dumps(rust_versions))}" />''', 'html.parser'))
    resources = BeautifulSoup(resources_html, 'html.parser')
    styles = BeautifulSoup(f"<link rel=\"stylesheet\" href=\"{prefix}assets/rustdoc/css/rustdoc-in-hotdoc.css\" />", 'html.parser')
    head.append(resources)
    head.append(styles)

    # Add hotdoc navigation
    body = soup.find('body')
    if body and nav_html:
        nav = BeautifulSoup(nav_html, 'html.parser')
        first_child = body.find(True)
        if first_child:
            first_child.insert_before(nav)

    # Write modified content back to file
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(str(soup))

    return True


def remove_rustdoc_settings(soup):
    """Removes the entire rustdoc toolbar."""
    toolbar = soup.find('rustdoc-toolbar')
    if toolbar:
        toolbar.decompose()


def copy_rustdoc_integration_assets(docs_dir):
    """Ensures the rustdoc assets directory exists."""
    asset_dir = docs_dir / "assets" / "rustdoc"
    asset_dir.mkdir(parents=True, exist_ok=True)

    src_asset_dir = Path(__file__).parent / "assets"
    shutil.copytree(src_asset_dir, asset_dir, dirs_exist_ok=True)

    # Get list of non-sys Rust crates
    latest_file = docs_dir / "rust" / "stable" / "latest"
    try:
        with open(latest_file, 'r') as f:
            version = f.read().strip()
    except IsADirectoryError:
        version = 'latest'

    rust_docs_path = docs_dir / "rust" / "stable" / version / "docs"
    if not rust_docs_path.exists():
        print(f"Warning: Rust docs directory not found at {rust_docs_path}")
        return

    crates = []
    for item in rust_docs_path.iterdir():
        if item.is_dir() and not item.name.endswith('_sys') and (item / "index.html").exists():
            crates.append(item.name)

    crates.sort()  # Sort alphabetically
    crates_renames = {
        "gstreamer": "core",
        "gstreamer_gl": "OpenGL",
        "gstreamer_gl_egl": "OpenGL EGL",
        "gstreamer_gl_wayland": "OpenGL Wayland",
        "gstreamer_gl_x11": "OpenGL X11",
    }

    rs_fixer_script = asset_dir / "js/sitemap-rs-fixer.js"
    with rs_fixer_script.open("r") as f:
        script_template = f.read()

    # Replace the placeholder values
    script_content = script_template.replace(
        "CRATES_LIST", str(crates)
    ).replace(
        "CRATES_RENAMES", str(crates_renames)
    )

    # Write the modified script
    print('\nAdding crates information into sitemap-rs-fixer.js')
    with rs_fixer_script.open("w") as f:
        f.write(script_content)

    return asset_dir


def add_rust_api_menu(soup, prefix=''):
    """
    Adds a script to dynamically insert Rust into the language dropdown menu.

    Args:
        soup: BeautifulSoup object of the HTML content

    Returns:
        bool: True if modification was needed and successful, False otherwise
    """
    # Check if script already exists
    existing_scripts = soup.find_all('script')
    to_remove = []
    for script in existing_scripts:
        if 'language-menu.js' in script.get('src', ''):
            to_remove.append(script)
            break
    for script in to_remove:
        existing_scripts.remove(script)

    # Add script to the end of head
    head = soup.find('head')
    if not head:
        raise Exception("Failed to find <head> tag")
    script_tag = BeautifulSoup(f'<script src="{prefix}assets/rustdoc/js/language-menu.js" />', 'html.parser')

    head.append(script_tag)
    return True


def modify_hotdoc_html_file(file_path):
    """Modifies a single hotdoc HTML file to add the Rust API menu."""
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    soup = BeautifulSoup(content, 'html.parser')

    # Add Rust API menu
    success = add_rust_api_menu(soup)
    if not success:
        raise Exception("Failed to add Rust API menu")

    # Write modified content back to file
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(str(soup))
    return True


def is_rustdoc_file(path):
    # Convert to Path object and get parts
    parts = Path(path).parts

    # Check if path has any parts and if first part is "rust"
    return len(parts) > 0 and parts[0] == "rust"


def process_single_file(file_path, docs_path, counters):
    """
    Process a single HTML file with appropriate modifications.

    Args:
        file_path (Path): Path to the HTML file
        docs_path (Path): Root documentation directory
        counters (dict): Shared dictionary for counting successes/failures
    """
    try:
        if not is_rustdoc_file(file_path.relative_to(docs_path)):
            if modify_hotdoc_html_file(file_path):
                with counters['lock']:
                    counters['processed'] += 1
            else:
                with counters['lock']:
                    counters['failed'] += 1
            return

        if modify_rustdoc_html_file(file_path, docs_path):
            with counters['lock']:
                counters['processed'] += 1
        else:
            with counters['lock']:
                counters['failed'] += 1

        if sys.stdout.isatty():
            print(f"\rProcessed: {counters['processed'] + counters['failed']}/{counters['total']} files", end='')

    except Exception as e:
        print(f"\nError processing {file_path}: {repr(e)}")
        traceback.print_exc()
        with counters['lock']:
            counters['failed'] += 1


def has_ancestor(path: Path, ancestor: Path) -> bool:
    try:
        path.resolve().relative_to(ancestor.resolve())
        return True
    except ValueError:
        return False


def process_docs(docs_dir: Path):
    """
    Recursively processes all HTML files in the Rust documentation directory using parallel processing.

    Args:
        docs_dir (str): Path to the root of the Rust documentation
    """
    docs_path = Path(docs_dir).resolve()

    if not docs_path.exists():
        print(f"Error: Directory {docs_dir} does not exist")
        return

    sitemap_path = (docs_path / "hotdoc-sitemap.html").resolve()
    assets_dir = (docs_path / 'assets').resolve()

    def html_file_needs_processing(html_file):
        html_file = html_file.resolve()
        if has_ancestor(html_file, assets_dir):
            return False
        if html_file == sitemap_path:
            return False
        return True

    # Get list of all HTML files
    html_files = [html_file for html_file in docs_path.rglob('*.html') if
                  html_file_needs_processing(html_file)]

    # Create a manager for sharing counters between processes
    manager = Manager()
    counters = manager.dict({
        'processed': 0,
        'failed': 0,
        'total': len(html_files)
    })
    counters['lock'] = manager.Lock()

    # Calculate optimal number of processes
    num_processes = min(cpu_count(), len(html_files))

    print(f"\nStarting parallel processing with {num_processes} processes...")

    # Create a partial function with fixed arguments
    process_func = partial(process_single_file, docs_path=docs_path, counters=counters)

    # process_single_file(Path('/Users/thiblahute/fs-devel/gstreamer/tmptestdoc/documentation/rust/stable/0.23/docs/gstreamer/index.html'), docs_path, counters)
    # for html_file in html_files:
    #     process_func(html_file)
    # Process files in parallel
    with Pool(processes=num_processes) as pool:
        pool.map(process_func, html_files)

    print("\n\nProcessing complete:")
    print(f"Successfully processed: {counters['processed']} files")
    print(f"Failed to process: {counters['failed']} files")


def download_rust_docs(doc_dir):
    """
    Downloads the latest Rust documentation from rust-lang.org and unzips it.

    Args:
        doc_dir (str): Path to the directory where the documentation will be downloaded
    """
    print("Looking for rust latest rust documention")
    if not os.path.exists("rustdocs.zip"):
        rustdoc_url = get_documentation_artifact_url("gstreamer/gstreamer-rs",
                                                     "pages")

        def progress_hook(count, block_size, total_size):
            percent = int(count * block_size * 100 / total_size)
            sys.stdout.write(f"\rDownloading... {percent}%")
            sys.stdout.flush()

        print(f"Downloading rust documentation from {rustdoc_url}")
        urlretrieve(rustdoc_url, "rustdocs.zip", reporthook=progress_hook)

    print("Unpacking rust documentation")
    with zipfile.ZipFile('rustdocs.zip', 'r') as zip_ref:
        # Get list of files
        file_list = zip_ref.namelist()
        total_files = len(file_list)

        # Extract each file with progress
        for i, file in enumerate(file_list, 1):
            if file.endswith('html.gz'):
                continue

            zip_ref.extract(file, '.')
            if sys.stdout.isatty():
                print(f"\rExtracting: {i}/{total_files} files", end='')

    shutil.move("public", doc_dir / "rust")


def move_rust_latest(doc_dir):
    # Handle latest symlink
    latest_file = doc_dir / "rust" / "stable" / "latest"
    assert latest_file.exists
    if latest_file.is_dir():
        print(f'{latest_file} is already a directory')
        return

    # Read the version from the latest file
    with open(latest_file, 'r') as f:
        version = f.read().strip()

    # Remove the 'latest' file
    latest_file.unlink()

    # Create symlink from version directory to 'latest'
    version_dir = latest_file.parent / version
    assert version_dir.exists(), f"Version directory '{version}' not found"
    version_dir.rename(latest_file)
    print(f'Moved {version_dir} to {latest_file}')


def add_rust_fixer_to_sitemap(docs_root: Path):
    """Modifies the hotdoc-sitemap.html file to customize the Rust API references."""
    sitemap_path = docs_root / "hotdoc-sitemap.html"
    if not sitemap_path.exists():
        print(f"Warning: {sitemap_path} not found")
        return

    # Read the existing sitemap to extract the structure
    with open(sitemap_path, 'r', encoding='utf-8') as f:
        content = f.read()

    soup = BeautifulSoup(content, 'html.parser')

    head = soup.find('head')
    assert head, "Failed to find <head> tag"
    for script in head.find_all('script'):
        src = script.get('src', '')
        if 'sitemap-rs-fixer.js' in src:
            print('hotdoc-sitemap.html already contains the script')
            return

    # The utils script is required byt the fixer to detect
    # the configured language
    head.append(BeautifulSoup('''
    <script src="assets/js/utils.js" />
    <script src="assets/rustdoc/js/sitemap-rs-fixer.js" />
                ''', 'html.parser'))

    with open(sitemap_path, 'w', encoding='utf-8') as f:
        f.write(str(soup))


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        print("Usage: python modify_rust_docs.py <rust_docs_directory>")
        sys.exit(1)

    docs_dir = Path(sys.argv[1])
    download_rust_docs(docs_dir)
    move_rust_latest(docs_dir)
    copy_rustdoc_integration_assets(docs_dir)
    process_docs(docs_dir)
    add_rust_fixer_to_sitemap(docs_dir)
