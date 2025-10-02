/**
 * SVG Overlay Manager
 *
 * Main coordinator for all SVG overlay functionality including tooltips,
 * text ellipsizing, pipeline navigation, and drag-to-pan interaction.
 */

class SvgOverlayManager {
    constructor() {
        this.tooltipManager = null;
        this.pipelineNavigationManager = null;
        this.textEllipsizerManager = null;
        this.gv = null;
    }

    /**
     * Initializes all managers and sets up the SVG overlay functionality
     */
    init() {
        // Initialize managers
        this.tooltipManager = new TooltipManager();
        this.pipelineNavigationManager = new PipelineNavigationManager(this.tooltipManager);
        this.textEllipsizerManager = new TextEllipsizerManager(this.tooltipManager, this.pipelineNavigationManager);

        // Set up GraphViz SVG functionality
        this.setupGraphvizSvg();
    }

    /**
     * Sets up GraphViz SVG functionality and event handlers
     */
    setupGraphvizSvg() {
        $("#graph").graphviz({
            url: this.getSvgUrl(),
            ready: () => {
                this.onSvgReady();
            }
        });
    }

    /**
     * Called when SVG is loaded and ready
     */
    onSvgReady() {
        this.gv = $("#graph").data('graphviz.svg');
        const $svg = $("#graph svg");

        // Set up node click functionality for highlighting
        this.setupNodeHighlighting();

        // Setup cluster zooms
        this.setupClusterZoom();

        // Set up smart drag-to-pan behavior that doesn't interfere with text selection
        this.setupSmartDragBehavior();

        // Set up keyboard shortcuts
        this.setupKeyboardShortcuts();

        // Set up save SVG functionality
        this.setupSaveSvg();

        // Process SVG content
        this.processSvgContent($svg);
    }

    /**
     * Processes SVG content with all managers
     * @param {jQuery} $svg - jQuery object containing the SVG element
     */
    processSvgContent($svg) {
        // Set up pipeline-dot navigation
        this.pipelineNavigationManager.setupPipelineDotNavigation($svg);

        // Process text ellipsizing (this must come after pipeline navigation setup)
        this.textEllipsizerManager.ellipsizeLongText($svg);
    }

    /**
     * Sets up node click functionality for highlighting connected nodes
     */
    setupNodeHighlighting() {
        this.gv.nodes().on('click', function () {
            const gv = $("#graph").data('graphviz.svg');
            let $set = $();
            $set.push(this);
            $set = $set.add(gv.linkedFrom(this, true));
            $set = $set.add(gv.linkedTo(this, true));
            gv.highlight($set, true);
            gv.bringToFront($set);
        });
    }

    setupClusterZoom() {
        this.gv.clusters().on('dblclick', function () {
            const gv = $("#graph").data('graphviz.svg');
            gv.scaleToNode($(this));
        });
    }

    /**
     * Sets up smart drag behavior that allows text selection while preserving pan functionality
     */
    setupSmartDragBehavior() {
        const graphDiv = document.getElementById('graph');

        // Intercept mousedown events to prevent dragscroll on text elements
        graphDiv.addEventListener('mousedown', (e) => {
            if (e.target.tagName === 'text' || e.target.tagName === 'tspan') {
                if (e.target.textContent &&
                    e.target.textContent.startsWith("pipeline-dot=") &&
                    e.target.textContent.includes(".dot")) {
                    return; // Let pipeline dot click handler take precedence
                }
                e.stopPropagation(); // Stop dragscroll for regular text
                return true;
            }
        }, true);
    }

    /**
     * Sets up keyboard shortcuts for the SVG viewer
     */
    setupKeyboardShortcuts() {
        $(document).on('keyup', (evt) => {
            if (evt.key == "Escape") {
                this.gv.highlight();
            } else if (evt.key == "w") {
                this.gv.scaleInView((this.gv.zoom.percentage + 100));
            } else if (evt.key == "s") {
                const newPercentage = Math.max(100, this.gv.zoom.percentage - 100);
                this.gv.scaleInView(newPercentage);
            }
        });
    }

    /**
     * Sets up SVG save functionality
     */
    setupSaveSvg() {
        $("#save-svg").on('click', () => {
            const svgElement = $("#graph svg")[0];
            const svgData = new XMLSerializer().serializeToString(svgElement);
            const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });
            const url = URL.createObjectURL(blob);
            const title = document.getElementById("title").textContent.trim();
            const downloadLink = document.createElement("a");
            downloadLink.href = url;
            downloadLink.download = title + ".svg";
            document.body.appendChild(downloadLink);
            downloadLink.click();
            document.body.removeChild(downloadLink);
            URL.revokeObjectURL(url);
        });
    }

    /**
     * Gets the SVG URL from query parameters
     * @returns {string} SVG URL
     */
    getSvgUrl() {
        const urlParams = new URLSearchParams(window.location.search);
        return urlParams.get('svg');
    }

    /**
     * Gets the title from query parameters and sets it in the document
     */
    setTitle() {
        const urlParams = new URLSearchParams(window.location.search);
        const title = urlParams.get('title');
        if (title) {
            document.getElementById("title").textContent = title;
            document.title = "Dots viewer: " + title;
        }
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = SvgOverlayManager;
} else {
    window.SvgOverlayManager = SvgOverlayManager;
}
