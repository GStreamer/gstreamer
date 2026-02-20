/**
 * Custom tooltip functionality for SVG elements
 *
 * Provides hoverable tooltips with interactive copy/paste mode for ellipsized text.
 * Interactive mode allows users to select and copy tooltip content.
 */

class TooltipManager {
    constructor() {
        this.tooltipEl = null;
        this.tooltipText = '';
        this.isTooltipInteractive = false;
        this.currentTooltipElement = null;

        this.init();
    }

    init() {
        this.tooltipEl = document.createElement('div');
        this.tooltipEl.className = 'custom-tooltip';
        document.body.appendChild(this.tooltipEl);

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
        this.tooltipEl.textContent = text;
        this.tooltipEl.style.left = (event.pageX + 10) + 'px';
        this.tooltipEl.style.top = (event.pageY - 30) + 'px';
        this.tooltipEl.classList.remove('interactive');
        this.tooltipEl.classList.add('show');
        this.isTooltipInteractive = false;
    }

    /**
     * Makes tooltip interactive (selectable and fixed position)
     */
    makeTooltipInteractive() {
        if (!this.isTooltipInteractive && this.currentTooltipElement) {
            this.tooltipEl.classList.add('interactive');
            this.isTooltipInteractive = true;

            const elementRect = this.currentTooltipElement.getBoundingClientRect();

            this.tooltipEl.style.left = Math.min(elementRect.left + window.scrollX + 20, window.innerWidth - 420) + 'px';
            this.tooltipEl.style.top = Math.max(elementRect.top + window.scrollY - 80, 10) + 'px';
            this.tooltipEl.style.pointerEvents = 'auto';

            // Remove mouseleave handler from the element
            if (this.currentTooltipElement._tooltipMouseleave) {
                this.currentTooltipElement.removeEventListener('mouseleave', this.currentTooltipElement._tooltipMouseleave);
            }

            // Select all text when made interactive
            setTimeout(() => {
                const range = document.createRange();
                range.selectNodeContents(this.tooltipEl);
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
        this.tooltipEl.classList.remove('show', 'interactive');
        this.isTooltipInteractive = false;
        this.currentTooltipElement = null;
    }

    /**
     * Sets up document click handler to hide tooltips when clicking outside
     */
    setupDocumentClickHandler() {
        document.addEventListener('click', (e) => {
            if (this.isTooltipInteractive) {
                if (!e.target.closest('.custom-tooltip') &&
                    !e.target.hasAttribute('data-has-tooltip') &&
                    e.target !== this.currentTooltipElement) {
                    this.hideTooltip();
                }
            } else {
                if (!e.target.hasAttribute('data-has-tooltip')) {
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
