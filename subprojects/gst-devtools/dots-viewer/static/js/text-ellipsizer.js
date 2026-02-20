/**
 * Text ellipsizing functionality for SVG elements
 *
 * Ellipsizes long text content (>80 chars) and provides tooltips with full content.
 * Integrates with tooltip system for interactive copy/paste functionality.
 */

class TextEllipsizerManager {
    constructor(tooltipManager, pipelineNavigationManager, capsHtmlFormatter = null) {
        this.tooltipManager = tooltipManager;
        this.pipelineNavigationManager = pipelineNavigationManager;
        this.maxLength = 80;
        this.capsHtmlFormatter = capsHtmlFormatter;
    }

    /**
     * Processes all text elements in SVG to ellipsize long content
     * @param {Element} svg - SVG element
     */
    ellipsizeLongText(svg) {
        svg.querySelectorAll('text, tspan').forEach(element => {
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

        element.textContent = ellipsizedText;

        element.dataset.originalText = originalText;
        element.setAttribute('data-has-tooltip', 'true');

        element.style.cursor = 'help';

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
        const capsHtml = this.capsHtmlFormatter ? this.capsHtmlFormatter(originalText) : null;
        const showFn = capsHtml
            ? (e) => this.tooltipManager.showHtmlTooltip(element, capsHtml, e)
            : (e) => this.tooltipManager.showTooltip(element, originalText, e);

        element.addEventListener('mouseenter', showFn);

        element.addEventListener('mousemove', (e) => {
            if (!this.tooltipManager.isInteractive()) showFn(e);
        });

        const mouseleaveHandler = () => {
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }
        };
        element._tooltipMouseleave = mouseleaveHandler;
        element.addEventListener('mouseleave', mouseleaveHandler);

        element.addEventListener('dblclick', (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (this.tooltipManager.tooltipEl.classList.contains('show')) {
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
        this.pipelineNavigationManager.makePipelineDotClickable(element, originalText);

        element.addEventListener('mouseenter', (e) => {
            element.style.color = '#0056b3';
            element.style.cursor = 'pointer';
            this.tooltipManager.showTooltip(element, originalText, e);
        });

        element.addEventListener('mousemove', (e) => {
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.showTooltip(element, originalText, e);
            }
        });

        const mouseleaveHandler = () => {
            element.style.color = '#007acc';
            element.style.cursor = 'pointer';
            if (!this.tooltipManager.isInteractive()) {
                this.tooltipManager.hideTooltip();
            }
        };
        element._tooltipMouseleave = mouseleaveHandler;
        element.addEventListener('mouseleave', mouseleaveHandler);

        element.addEventListener('contextmenu', (e) => {
            if (this.tooltipManager.tooltipEl.classList.contains('show')) {
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
