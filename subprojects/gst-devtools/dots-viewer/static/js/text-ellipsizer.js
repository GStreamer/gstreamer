/**
 * Text ellipsizing functionality for SVG elements
 *
 * Ellipsizes long text content (>80 chars) and provides tooltips with full content.
 * Integrates with tooltip system for interactive copy/paste functionality.
 */

class TextEllipsizerManager {
    constructor(tooltipManager, pipelineNavigationManager) {
        this.tooltipManager = tooltipManager;
        this.pipelineNavigationManager = pipelineNavigationManager;
        this.maxLength = 80;
    }

    /**
     * Processes all text elements in SVG to ellipsize long content
     * @param {jQuery} $svg - jQuery object containing the SVG element
     */
    ellipsizeLongText($svg) {
        $svg.find("text, tspan").each((index, element) => {
            const text = element.textContent;

            if (this.shouldEllipsize(text)) {
                this.ellipsizeElement(element, text);
            }
        });
    }

    /**
     * Determines if text should be ellipsized
     * @param {string} text - Text content to check
     * @returns {boolean} True if text should be ellipsized
     */
    shouldEllipsize(text) {
        return text && text.length > this.maxLength;
    }

    /**
     * Ellipsizes an element and sets up tooltip functionality
     * @param {Element} element - DOM element to ellipsize
     * @param {string} originalText - Original full text content
     */
    ellipsizeElement(element, originalText) {
        const ellipsizedText = originalText.substring(0, 77) + "...";

        // Update the text content
        $(element).text(ellipsizedText);

        // Store original text and mark as having tooltip
        $(element).data('original-text', originalText);
        $(element).attr('data-has-tooltip', 'true');

        // Style to indicate there's more content
        $(element).css({
            'cursor': 'help'
        });

        this.setupTooltipHandlers(element, originalText);
    }

    /**
     * Sets up tooltip handlers for ellipsized elements
     * @param {Element} element - DOM element to add handlers to
     * @param {string} originalText - Original full text content
     */
    setupTooltipHandlers(element, originalText) {
        const isPipelineDot = this.pipelineNavigationManager.isPipelineDotReference(originalText);

        if (!isPipelineDot) {
            this.setupRegularTooltipHandlers(element, originalText);
        } else {
            this.setupPipelineDotTooltipHandlers(element, originalText);
        }
    }

    /**
     * Sets up tooltip handlers for regular (non-pipeline-dot) elements
     * @param {Element} element - DOM element to add handlers to
     * @param {string} originalText - Original full text content
     */
    setupRegularTooltipHandlers(element, originalText) {
        $(element).on('mouseenter', (e) => {
            this.tooltipManager.showTooltip(element, originalText, e);
        });

        $(element).on('mousemove', (e) => {
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.showTooltip(element, originalText, e);
            }
        });

        $(element).on('mouseleave.tooltip', () => {
            // Don't hide tooltip on mouseleave if it's interactive
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }
        });

        // Double-click to make tooltip interactive
        $(element).on('dblclick', (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (this.tooltipManager.$tooltip.hasClass('show')) {
                this.tooltipManager.makeTooltipInteractive();
            }
        });
    }

    /**
     * Sets up tooltip handlers for pipeline-dot elements (with navigation functionality)
     * @param {Element} element - DOM element to add handlers to
     * @param {string} originalText - Original full text content
     */
    setupPipelineDotTooltipHandlers(element, originalText) {
        // Make it clickable for navigation
        this.pipelineNavigationManager.makePipelineDotClickable(element, originalText);

        // Add tooltip functionality
        $(element).on('mouseenter', (e) => {
            $(element).css({
                'color': '#0056b3',
                'cursor': 'pointer'
            });
            this.tooltipManager.showTooltip(element, originalText, e);
        });

        $(element).on('mousemove', (e) => {
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.showTooltip(element, originalText, e);
            }
        });

        $(element).on('mouseleave.tooltip', () => {
            $(element).css({
                'color': '#007acc',
                'cursor': 'pointer'
            });
            // Don't hide tooltip on mouseleave if it's interactive
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }
        });

        // Right-click to make tooltip interactive (since left-click navigates)
        $(element).on('contextmenu', (e) => {
            if (this.tooltipManager.$tooltip.hasClass('show')) {
                e.preventDefault();
                e.stopPropagation();
                this.tooltipManager.makeTooltipInteractive();
            }
        });
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = TextEllipsizerManager;
} else {
    window.TextEllipsizerManager = TextEllipsizerManager;
}
