// Wait for the language dropdown to be created
const language_observer = new MutationObserver((mutations) => {
    mutations.forEach((mutation) => {
        if (mutation.addedNodes.length) {
            const languageDropdown = document.querySelector('ul.dropdown-menu li a[href*="gi-language="]');
            if (languageDropdown) {
                // Found the language dropdown, so we can add Rust
                const dropdownMenu = languageDropdown.closest('ul.dropdown-menu');
                if (dropdownMenu && !dropdownMenu.querySelector('a[href*="rust"]')) {
                    // Create new list item for Rust
                    const rustItem = document.createElement('li');
                    const rustLink = document.createElement('a');
                    rustLink.href = 'rust/stable/latest/docs/index.html?gi-language=rust';
                    rustLink.textContent = 'rust';
                    rustItem.appendChild(rustLink);
                    dropdownMenu.appendChild(rustItem);

                    // Disconnect language_observer since we've done our work
                    language_observer.disconnect();
                }
            }
        }
    });
});


function transformVersionMenu(versions) {
    if (!versions) {
        console.error("hotdoc rustdoc version could not be loaded");
        return;
    }
    // Find the menu
    const menu = document.getElementById('API versions-menu');
    if (!menu) {
        console.error("API versions menu not found");
        return;
    }

    // Remove the Reset option and divider
    const resetItem = menu.querySelector('li:first-child');
    const divider = menu.querySelector('.divider');
    if (resetItem) resetItem.remove();
    if (divider) divider.remove();

    for (const [key, value] of Object.entries(versions)) {
        const link = Array.from(menu.getElementsByTagName('a'))
            .find(a => a.textContent.trim() === key);

        assert(link);
        link.href = value;
    }
}

// Start observing the navbar for changes
document.addEventListener('DOMContentLoaded', () => {
    const navbar = document.querySelector('#navbar-wrapper');
    if (navbar) {
        language_observer.observe(navbar, {
            childList: true,
            subtree: true
        });
    }

    let versions = JSON.parse(document.getElementById('hotdoc-rust-info')
        .getAttribute("hotdoc-rustdoc-versions")
        .replace(/'/g, '"'));


    createTagsDropdown({ "API versions": Object.keys(versions) });

    transformVersionMenu(versions);
});

function syncSidenavIframeParams() {
    const params = new URLSearchParams(window.location.search);
    const language = params.get('gi-language');
    const frame = document.getElementById('sitenav-frame');

    if (frame) {
        const frameUrl = new URL(frame.src, window.location.origin);
        if (language) {
            frameUrl.searchParams.set('gi-language', language);
        } else {
            frameUrl.searchParams.delete('gi-language');
        }
        frame.src = frameUrl.toString();
    }
}

document.addEventListener('DOMContentLoaded', syncSidenavIframeParams);


