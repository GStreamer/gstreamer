/**
 * Custom tooltip functionality for SVG elements
 *
 * Provides hoverable tooltips with interactive copy/paste mode for ellipsized text.
 * Interactive mode allows users to select and copy tooltip content.
 */

class TooltipManager {
    constructor() {
        this.$tooltip = null;
        this.tooltipText = '';
        this.isTooltipInteractive = false;
        this.currentTooltipElement = null;

        this.init();
    }

    init() {
        // Create custom tooltip element
        this.$tooltip = $('<div class="custom-tooltip"></div>').appendTo('body');

        // Set up document click handler for hiding tooltips
        this.setupDocumentClickHandler();
    }

    /**
     * Shows tooltip with given text at event position
     * @param {Element} element - Element that triggered the tooltip
     * @param {string} text - Text to display in tooltip
     * @param {Event} event - Mouse event for positioning
     */
    showTooltip(element, text, event) {
        this.tooltipText = text;
        this.currentTooltipElement = element;
        this.$tooltip.text(text);
        this.$tooltip.css({
            left: event.pageX + 10 + 'px',
            top: event.pageY - 30 + 'px'
        }).removeClass('interactive').addClass('show');
        this.isTooltipInteractive = false;
    }

    /**
     * Makes tooltip interactive (selectable and fixed position)
     */
    makeTooltipInteractive() {
        if (!this.isTooltipInteractive && this.currentTooltipElement) {
            this.$tooltip.addClass('interactive');
            this.isTooltipInteractive = true;

            // Position tooltip in a fixed position relative to the element
            const elementOffset = $(this.currentTooltipElement).offset();

            this.$tooltip.css({
                left: Math.min(elementOffset.left + 20, window.innerWidth - 420) + 'px',
                top: Math.max(elementOffset.top - 80, 10) + 'px',
                'pointer-events': 'auto'
            });

            // Prevent mouseleave from hiding the tooltip by removing the handler temporarily
            $(this.currentTooltipElement).off('mouseleave.tooltip');

            // Select all text when made interactive
            setTimeout(() => {
                const range = document.createRange();
                range.selectNodeContents(this.$tooltip[0]);
                const selection = window.getSelection();
                selection.removeAllRanges();
                selection.addRange(range);
            }, 10);
        }
    }

    /**
     * Hides the tooltip and resets state
     */
    hideTooltip() {
        this.$tooltip.removeClass('show interactive');
        this.isTooltipInteractive = false;
        this.currentTooltipElement = null;
    }

    /**
     * Sets up document click handler to hide tooltips when clicking outside
     */
    setupDocumentClickHandler() {
        $(document).on('click', (e) => {
            if (this.isTooltipInteractive) {
                // Only hide interactive tooltip when clicking outside both tooltip and original element
                if (!$(e.target).closest('.custom-tooltip').length &&
                    !$(e.target).is('[data-has-tooltip]') &&
                    e.target !== this.currentTooltipElement) {
                    this.hideTooltip();
                }
            } else {
                // Hide non-interactive tooltip when clicking anywhere except on elements with tooltips
                if (!$(e.target).is('[data-has-tooltip]')) {
                    this.hideTooltip();
                }
            }
        });
    }

    /**
     * Checks if tooltip is currently in interactive mode
     * @returns {boolean} True if tooltip is interactive
     */
    isInteractive() {
        return this.isTooltipInteractive;
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = TooltipManager;
} else {
    window.TooltipManager = TooltipManager;
}
