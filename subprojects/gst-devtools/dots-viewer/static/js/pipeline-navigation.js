/**
 * Pipeline dot navigation functionality
 *
 * Makes pipeline-dot references clickable for navigation between pipeline views.
 * Handles URL generation and navigation for pipeline dot references.
 */

class PipelineNavigationManager {
    constructor(tooltipManager) {
        this.tooltipManager = tooltipManager;
    }

    /**
     * Processes all pipeline-dot references in SVG to make them clickable
     * @param {jQuery} $svg - jQuery object containing the SVG element
     */
    setupPipelineDotNavigation($svg) {
        $svg.find(".cluster").each((index, cluster) => {
            $(cluster).find("text, tspan").each((index, element) => {
                const text = element.textContent;
                if (this.isPipelineDotReference(text)) {
                    this.makePipelineDotClickable(element, text);
                }
            });
        });
    }

    /**
     * Checks if text content is a pipeline-dot reference
     * @param {string} text - Text content to check
     * @returns {boolean} True if text is a pipeline-dot reference
     */
    isPipelineDotReference(text) {
        return text && text.startsWith("pipeline-dot=") && text.includes(".dot");
    }

    /**
     * Makes a pipeline-dot element clickable with proper styling and handlers
     * @param {Element} element - DOM element to make clickable
     * @param {string} text - Original text content
     */
    makePipelineDotClickable(element, text) {
        const pipelineDot = this.extractPipelineDotFilename(text);

        // Style as a clickable link
        $(element).css({
            'text-decoration': 'underline',
            'cursor': 'pointer',
            'color': '#007acc'
        });

        // Add click handler for navigation
        $(element).off('click.pipeline-nav').on('click.pipeline-nav', (evt) => {
            evt.preventDefault();
            evt.stopPropagation();

            // Only hide tooltip if it's not in interactive mode
            if (this.tooltipManager && !this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }

            this.navigateToPipeline(pipelineDot);
        });

        // Add hover effects
        this.setupHoverEffects(element);
    }

    /**
     * Extracts the pipeline dot filename from the full text
     * @param {string} text - Full pipeline-dot text
     * @returns {string} Extracted filename
     */
    extractPipelineDotFilename(text) {
        let pipelineDot = text;
        if (pipelineDot.includes("pipeline-dot=\"")) {
            pipelineDot = pipelineDot.replace(/pipeline-dot="([^"]+)"/, "$1");
        } else {
            pipelineDot = pipelineDot.replace(/pipeline-dot=([^\s]+)/, "$1");
        }
        return pipelineDot;
    }

    /**
     * Sets up hover effects for clickable pipeline-dot links
     * @param {Element} element - DOM element to add hover effects to
     */
    setupHoverEffects(element) {
        $(element).on('mouseenter', function () {
            $(this).css({
                'color': '#0056b3',
                'cursor': 'pointer'
            });
        }).on('mouseleave', function () {
            $(this).css({
                'color': '#007acc',
                'cursor': 'pointer'
            });
        });
    }

    /**
     * Navigates to the specified pipeline
     * @param {string} pipelineDot - Pipeline dot filename to navigate to
     */
    navigateToPipeline(pipelineDot) {
        // Check if we're in an iframe (overlay.html context)
        if (window.parent !== window) {
            // We're in an iframe, navigate the parent window
            const newUrl = window.parent.location.origin + "/?pipeline=" + encodeURIComponent(pipelineDot);
            window.parent.location.href = newUrl;
        } else {
            // We're in the main window
            const newUrl = window.location.origin + "/?pipeline=" + encodeURIComponent(pipelineDot);
            window.location.href = newUrl;
        }
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = PipelineNavigationManager;
} else {
    window.PipelineNavigationManager = PipelineNavigationManager;
}
