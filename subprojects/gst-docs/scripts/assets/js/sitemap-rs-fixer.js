function modifyLibsPanel() {
    // Check if we should show Rust API
    const params = new URLSearchParams(window.location.search);
    var language = params.get('gi-language');

    if (!language) {
        language = utils.getStoredLanguage();
    }

    if (language === 'rust') {
        let apiref = document.querySelector('a[data-nav-ref="gi-extension-GStreamer-libs.html"]');
        apiref.innerText = "Rust crates";
        apiref.href = "rust/stable/latest/docs/index.html?gi-language=rust";

        // Add crates to the panel
        const crates = CRATES_LIST; // This will be replaced by Python
        const renames = CRATES_RENAMES; // This will be replaced by Python

        const rootDiv = document.querySelector('div[data-nav-ref="gi-extension-GStreamer-libs.html"]');
        if (!rootDiv) {
            console.error('Root div not found');
            return;
        }
        // Now iterate first level panel bodies to make links point to rust
        // crates
        const firstLevelPanels = rootDiv.querySelectorAll(':scope > div.sidenav-panel-body');
        firstLevelPanels.forEach((panel, index) => {
            if (index >= crates.length) {
                panel.remove();
                return;
            }

            const crate = crates[index];
            const link = panel.querySelector('a');

            if (link) {
                // Update href
                link.setAttribute('href', `rust/stable/latest/docs/${crate}/index.html?gi-language=rust`);

                // Update text content
                link.textContent = renames[crate] ||
                    crate.replace("gstreamer", "")
                        .replace(/_/g, " ")
                        .trim()
                        .split(" ")
                        .map(word => word.charAt(0).toUpperCase() + word.slice(1))
                        .join(" ");

                // Remove children of the library item
                const navRef = link.getAttribute('data-nav-ref');
                if (navRef) {
                    const siblingDiv = rootDiv.querySelector(`div[data-nav-ref="${navRef}"]`);
                    if (siblingDiv) {
                        siblingDiv.remove();
                    }
                }

                // And now remove the glyphicons (arrows) as we removed the
                // children
                const linkContainer = link.closest('div');
                if (linkContainer) {
                    const glyphicons = linkContainer.querySelectorAll('.glyphicon');
                    glyphicons.forEach(icon => icon.remove());
                }

            }
        });
    }
}

// Run when page loads and when URL changes
document.addEventListener('DOMContentLoaded', modifyLibsPanel);
window.addEventListener('popstate', modifyLibsPanel);
window.addEventListener('pushstate', modifyLibsPanel);
window.addEventListener('replacestate', modifyLibsPanel);

