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
     * @param {Element} svg - SVG element
     */
    setupPipelineDotNavigation(svg) {
        svg.querySelectorAll('.cluster').forEach(cluster => {
            cluster.querySelectorAll('text, tspan').forEach(element => {
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

        element.style.textDecoration = 'underline';
        element.style.cursor = 'pointer';
        element.style.color = '#007acc';

        element.addEventListener('click', (evt) => {
            evt.preventDefault();
            evt.stopPropagation();

            if (this.tooltipManager && !this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }

            this.navigateToPipeline(pipelineDot);
        });

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
        element.addEventListener('mouseenter', function () {
            this.style.color = '#0056b3';
            this.style.cursor = 'pointer';
        });
        element.addEventListener('mouseleave', function () {
            this.style.color = '#007acc';
            this.style.cursor = 'pointer';
        });
    }

    /**
     * Navigates to the specified pipeline
     * @param {string} pipelineDot - Pipeline dot filename to navigate to
     */
    navigateToPipeline(pipelineDot) {
        const topWindow = window.parent !== window ? window.parent : window;
        const newUrl = topWindow.location.origin + "/?pipeline=" + encodeURIComponent(pipelineDot);
        topWindow.location.href = newUrl;
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = PipelineNavigationManager;
} else {
    window.PipelineNavigationManager = PipelineNavigationManager;
}
