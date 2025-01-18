function syncThemeWithHotdoc() {
    // Get the current stylesheet
    const currentStyle = document.querySelector('link[rel="stylesheet"][href*="frontend"]');
    if (!currentStyle) return;

    // Check if we're using dark theme in hotdoc
    const isDark = getActiveStyleSheet() == 'dark';

    // Use rustdoc's switchTheme function to set the theme
    let newThemeName = isDark ? 'dark' : 'light';
    window.switchTheme(newThemeName, true);
}

// Run on page load
document.addEventListener('DOMContentLoaded', () => {
    localStorage.setItem("rustdoc-use-system-theme", false);
    syncThemeWithHotdoc();

    // Watch for theme changes in hotdoc
    const theme_observer = new MutationObserver((mutations) => {
        mutations.forEach((mutation) => {
            if (mutation.type === 'attributes' && mutation.attributeName === 'disabled') {
                syncThemeWithHotdoc();
            }
        });
    });

    // Start observing theme changes
    const styleLink = document.querySelector('link[rel="stylesheet"][href*="frontend"]');
    if (styleLink) {
        theme_observer.observe(styleLink, { attributes: true });
    }
});

