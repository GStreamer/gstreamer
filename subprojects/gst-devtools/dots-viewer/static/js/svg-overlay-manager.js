/**
 * SVG Overlay Manager
 *
 * Main coordinator for all SVG overlay functionality including tooltips,
 * text ellipsizing, pipeline navigation, and node highlighting.
 *
 * Uses d3-graphviz for rendering DOT content directly into SVG.
 */

class SvgOverlayManager {
    constructor() {
        this.tooltipManager = null;
        this.pipelineNavigationManager = null;
        this.textEllipsizerManager = null;
        this.pipelineTitle = '';
        this._nodesByName = {};
        this._edgesByName = {};
        this._highlighted = false;
        this._abortController = new AbortController();
    }

    /**
     * Tears down document-level event listeners added by this manager.
     * Call when the overlay is removed from the DOM.
     */
    destroy() {
        this._abortController.abort();
    }

    /**
     * Initializes all managers and renders the DOT content with d3-graphviz
     * @param {string} dotContent - DOT graph content to render
     * @param {string} pipelineTitle - Title of the pipeline
     */
    init(dotContent, pipelineTitle) {
        this.pipelineTitle = pipelineTitle || '';

        this.tooltipManager = new TooltipManager();
        this.pipelineNavigationManager = new PipelineNavigationManager(this.tooltipManager);
        this.textEllipsizerManager = new TextEllipsizerManager(this.tooltipManager, this.pipelineNavigationManager);

        this.gv = d3.select("#graph").graphviz()
            .zoom(true)
            .fit(true)
            .on('end', () => this.onSvgReady())
            .renderDot(dotContent);
    }

    /**
     * Called when d3-graphviz has finished rendering the SVG
     */
    onSvgReady() {
        const svg = document.querySelector('#graph svg');

        this._fitSvgToContainer(svg);
        this._extendZoomRange();
        this._setupScrollBehavior();
        this._indexElements(svg);
        this.setupNodeHighlighting();
        this.setupClusterZoom();
        this.setupHoverEffects();
        this.setupSmartDragBehavior();
        this.setupKeyboardShortcuts();
        this.setupSaveSvg();
        this.processSvgContent(svg);
    }

    /**
     * Extends d3-graphviz's default zoom scale extent so users can zoom in
     * far enough on large graphs.
     */
    _extendZoomRange() {
        try {
            const zoomBehavior = this.gv.zoomBehavior();
            if (zoomBehavior) {
                zoomBehavior.scaleExtent([0.01, 500]);
                /* Disable d3-zoom's built-in double-click-to-zoom so it
                   doesn't fight with our cluster double-click handler. */
                const zoomSelection = this.gv.zoomSelection();
                if (zoomSelection) {
                    zoomSelection.on('dblclick.zoom', null);
                }
            }
        } catch (e) {
            /* graphviz may not expose zoom behavior in all cases */
        }
    }

    /**
     * Replaces d3-graphviz's default wheel-to-zoom with:
     *   - plain scroll / trackpad two-finger swipe → pan
     *   - Ctrl+scroll / trackpad pinch (ctrlKey) → zoom around cursor
     */
    _setupScrollBehavior() {
        try {
            const zoomBehavior = this.gv.zoomBehavior();
            const zoomSelection = this.gv.zoomSelection();
            if (!zoomBehavior || !zoomSelection) return;

            const svgEl = zoomSelection.node();

            /* Remove d3-zoom's built-in wheel-to-zoom handler */
            zoomSelection.on('wheel.zoom', null);

            svgEl.addEventListener('wheel', (evt) => {
                evt.preventDefault();

                const current = svgEl.__zoom || d3.zoomIdentity;

                /* Normalise delta to CSS pixels */
                const px = evt.deltaMode === 1 ? 18 : evt.deltaMode === 2 ? window.innerHeight : 1;
                const dx = evt.deltaX * px;
                const dy = evt.deltaY * px;

                if (evt.ctrlKey) {
                    /* Ctrl+wheel / trackpad pinch → zoom around the cursor */
                    const scaleFactor = Math.pow(2, -dy * 0.002);
                    const newK = Math.max(0.01, Math.min(500, current.k * scaleFactor));
                    const [mx, my] = d3.pointer(evt, svgEl);
                    const newX = mx - (newK / current.k) * (mx - current.x);
                    const newY = my - (newK / current.k) * (my - current.y);
                    zoomBehavior.transform(zoomSelection,
                        d3.zoomIdentity.translate(newX, newY).scale(newK));
                } else {
                    /* Plain scroll → pan */
                    const vb = svgEl.viewBox.baseVal;
                    if (!vb || !vb.width) return;
                    const rect = svgEl.getBoundingClientRect();
                    const toSvg = vb.width / rect.width;
                    zoomBehavior.transform(zoomSelection,
                        d3.zoomIdentity
                            .translate(current.x - dx * toSvg, current.y - dy * toSvg)
                            .scale(current.k));
                }
            }, { passive: false });
        } catch (e) {
            /* graphviz may not expose zoom behavior in all cases */
        }
    }

    /**
     * Ensures the SVG is properly sized to fit its container.
     * d3-graphviz .fit(true) can miscalculate when the container
     * hasn't finished layout, so we re-apply the fit here.
     * @param {SVGElement} svg - The rendered SVG element
     */
    _fitSvgToContainer(svg) {
        if (!svg) return;
        const container = svg.parentElement;
        if (!container) return;

        const containerW = container.clientWidth;
        const containerH = container.clientHeight;
        if (!containerW || !containerH) return;

        /* read the native graph size from the viewBox or the SVG attributes */
        let vb = svg.viewBox.baseVal;
        if (!vb || (!vb.width && !vb.height)) {
            const w = parseFloat(svg.getAttribute('width'));
            const h = parseFloat(svg.getAttribute('height'));
            if (w && h) {
                svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
                vb = svg.viewBox.baseVal;
            }
        }

        if (vb && vb.width && vb.height) {
            svg.setAttribute('width', '100%');
            svg.setAttribute('height', '100%');
            svg.style.width = '100%';
            svg.style.height = '100%';
        }
    }

    /**
     * Indexes nodes and edges from SVG title elements for name-based lookup
     * @param {Element} svg - SVG element
     */
    _indexElements(svg) {
        this._nodesByName = {};
        this._edgesByName = {};

        svg.querySelectorAll('.node').forEach(node => {
            const title = node.querySelector('title');
            if (title) {
                const name = title.textContent.replace(/:[snew][ew]?/g, '');
                this._nodesByName[name] = node;
                node.setAttribute('data-name', name);
            }
        });

        svg.querySelectorAll('.edge').forEach(edge => {
            const title = edge.querySelector('title');
            if (title) {
                const name = title.textContent.replace(/:[snew][ew]?/g, '');
                this._edgesByName[name] = edge;
                edge.setAttribute('data-name', name);
            }
        });
    }

    /**
     * Finds all nodes and edges connected to the given node, recursively
     * following the full ghostpad/proxypad chain in both directions.
     * @param {string} nodeName - Name of the node to find connections for
     * @returns {Set<Element>} Set of connected elements (nodes + edges)
     */
    _findConnected(nodeName) {
        const result = new Set();
        const node = this._nodesByName[nodeName];
        if (node) result.add(node);

        this._findLinked(nodeName, true, true, result);
        this._findLinked(nodeName, false, true, result);

        return result;
    }

    /**
     * Recursively follows edges from a node in one direction.
     * @param {string} nodeName - Current node name
     * @param {boolean} forward - true for downstream (src->X), false for upstream (X->dst)
     * @param {boolean} includeEdges - Whether to include edge elements in the result
     * @param {Set<Element>} result - Accumulated result set
     */
    _findLinked(nodeName, forward, includeEdges, result) {
        const connectedNames = [];

        for (const [edgeName, edgeEl] of Object.entries(this._edgesByName)) {
            let otherName = null;
            if (forward) {
                const prefix = nodeName + '->';
                if (edgeName.startsWith(prefix)) {
                    otherName = edgeName.substring(prefix.length);
                }
            } else {
                const suffix = '->' + nodeName;
                if (edgeName.endsWith(suffix)) {
                    otherName = edgeName.substring(0, edgeName.length - suffix.length);
                }
            }

            if (otherName !== null) {
                if (includeEdges) {
                    result.add(edgeEl);
                }
                connectedNames.push(otherName);
            }
        }

        for (const name of connectedNames) {
            const n = this._nodesByName[name];
            if (n && !result.has(n)) {
                result.add(n);
                this._findLinked(name, forward, includeEdges, result);
            }
        }
    }

    /**
     * Highlights the given elements and dims everything else.
     *
     * Dimmed elements use opacity 0.35 + grayscale rather than a very low
     * opacity so that text on non-selected pads remains readable.
     * @param {Set<Element>|null} selected - Elements to highlight, or null to restore all
     */
    _highlight(selected) {
        const all = document.querySelectorAll('#graph .node, #graph .edge');

        if (selected && selected.size) {
            this._highlighted = true;
            all.forEach(el => {
                if (selected.has(el)) {
                    el.style.opacity = '1';
                    el.style.filter = '';
                } else {
                    el.style.opacity = '0.35';
                    el.style.filter = 'grayscale(1)';
                }
            });
            selected.forEach(el => {
                el.parentNode.appendChild(el);
            });
        } else {
            this._highlighted = false;
            all.forEach(el => {
                el.style.opacity = '1';
                el.style.filter = '';
            });
        }
    }

    /**
     * Sets up node click functionality for highlighting connected nodes
     */
    setupNodeHighlighting() {
        const that = this;

        document.querySelectorAll('#graph .node').forEach(node => {
            node.addEventListener('click', function(evt) {
                evt.stopPropagation();
                const nodeName = this.getAttribute('data-name');
                if (!nodeName) return;

                const connected = that._findConnected(nodeName);
                that._highlight(connected);
            });
        });

        /* Click on SVG background clears the highlight */
        const svg = document.querySelector('#graph svg');
        if (svg) {
            svg.addEventListener('click', () => {
                that._highlight(null);
            });
        }
    }

    /**
     * Sets up double-click on clusters to zoom-to-fit the cluster in the viewport.
     */
    setupClusterZoom() {
        const that = this;
        d3.select('#graph svg').selectAll('.cluster').on('dblclick', function(evt) {
            evt.stopPropagation();
            evt.preventDefault();
            that._zoomToElement(this);
        });
    }

    /**
     * Zooms the viewport to fit a given SVG element (e.g. a cluster).
     * d3-zoom's transform operates in SVG coordinate space (viewBox units),
     * not pixel space, so we must use the SVG's rendered dimensions.
     * @param {SVGElement} el - The SVG element to zoom to
     */
    _zoomToElement(el) {
        const svg = document.querySelector('#graph svg');
        if (!svg) return;

        const bbox = el.getBBox();
        if (!bbox.width || !bbox.height) return;

        /* d3-zoom's transform operates in the SVG's viewBox coordinate space.
         * getBBox() also returns viewBox-space coords. We must use viewBox
         * dimensions for the viewport, not pixel dimensions. */
        const vb = svg.viewBox.baseVal;
        if (!vb || !vb.width || !vb.height) return;
        const W = vb.width;
        const H = vb.height;

        /* Convert pixel padding to viewBox units so padding is consistent
         * regardless of the physical SVG size. */
        const graphDiv = document.getElementById('graph');
        const graphRect = graphDiv.getBoundingClientRect();
        const padPx = 40;
        const padX = padPx * (W / graphRect.width);
        const padY = padPx * (H / graphRect.height);

        const scale = Math.min(
            (W - 2 * padX) / bbox.width,
            (H - 2 * padY) / bbox.height
        );
        const cx = bbox.x + bbox.width / 2;
        const cy = bbox.y + bbox.height / 2;
        const tx = W / 2 - scale * cx;
        const ty = H / 2 - scale * cy;

        const target = d3.zoomIdentity.translate(tx, ty).scale(scale);

        try {
            const zoomBehavior = this.gv.zoomBehavior();
            const zoomSelection = this.gv.zoomSelection();
            const svgEl = zoomSelection.node();
            const start = svgEl.__zoom || d3.zoomIdentity;

            /* d3-zoom's built-in transition uses d3.interpolateZoom regardless
             * of the .interpolate() override, producing an incorrect final zoom.
             * Drive the animation ourselves with a rAF loop so we can use simple
             * linear interpolation (which gives the correct final transform). */
            const duration = 750;
            const ease = (t) => t < 0.5 ? 4*t*t*t : 1 - (-2*t + 2)**3 / 2;
            let startTime = null;
            const animate = (now) => {
                if (startTime === null) startTime = now;
                const et = ease(Math.min(1, (now - startTime) / duration));
                const k = start.k + et * (target.k - start.k);
                const x = start.x + et * (target.x - start.x);
                const y = start.y + et * (target.y - start.y);
                zoomBehavior.transform(zoomSelection,
                    d3.zoomIdentity.translate(x, y).scale(k));
                if (et < 1) requestAnimationFrame(animate);
            };
            requestAnimationFrame(animate);
        } catch (e) {
            d3.select('#graph svg > g').transition()
                .duration(750)
                .attr('transform', target.toString());
        }
    }

    /**
     * Adds a subtle brightness boost on hover to nodes (click to highlight)
     * and clusters (double-click to zoom), so users can see what is interactive.
     */
    setupHoverEffects() {
        document.querySelectorAll('#graph .node').forEach(node => {
            node.addEventListener('mouseenter', () => {
                node.style.filter = 'drop-shadow(0 0 5px rgba(66, 133, 244, 0.85))';
            });
            node.addEventListener('mouseleave', () => {
                node.style.filter = '';
            });
        });

        document.querySelectorAll('#graph .cluster').forEach(cluster => {
            cluster.addEventListener('mouseenter', () => {
                cluster.style.filter = 'drop-shadow(0 0 6px rgba(66, 133, 244, 0.6))';
            });
            cluster.addEventListener('mouseleave', () => {
                cluster.style.filter = '';
            });
        });
    }

    /**
     * Sets up smart drag behavior that allows text selection while preserving pan functionality
     */
    setupSmartDragBehavior() {
        const graphDiv = document.getElementById('graph');

        graphDiv.addEventListener('mousedown', (e) => {
            if (e.target.tagName === 'text' || e.target.tagName === 'tspan') {
                if (e.target.textContent &&
                    e.target.textContent.startsWith("pipeline-dot=") &&
                    e.target.textContent.includes(".dot")) {
                    return;
                }
                e.stopPropagation();
                return true;
            }
        }, true);
    }

    /**
     * Sets up keyboard shortcuts for the SVG viewer.
     *
     * Escape behaviour (two-press-to-close):
     *   • Overlay not open       – not intercepted at all (guard on #overlay).
     *   • Something highlighted  – first Esc unhighlights; second Esc closes.
     *   • Nothing highlighted    – first Esc is consumed and arms "pending";
     *                              second Esc lets the event through so
     *                              index.html's removePipelineOverlay() runs.
     *   • 2-second timeout       – resets a stale first-press so it never
     *                              blocks a later single-Esc close attempt.
     *
     * The listener runs in capture phase so it always fires before the
     * bubbling handler in index.html.
     */
    setupKeyboardShortcuts() {
        let escPending = false;
        let escTimer   = null;

        const resetPending = () => {
            escPending = false;
            clearTimeout(escTimer);
        };

        document.addEventListener('keyup', (evt) => {
            /* Only intercept while the overlay is actually in the DOM */
            if (!document.getElementById('overlay')) return;

            if (evt.key !== 'Escape') return;

            if (this._highlighted) {
                /* Unhighlight on first Esc; a subsequent Esc will close */
                evt.stopPropagation();
                this._highlight(null);
                resetPending();
            } else if (!escPending) {
                /* First Esc with nothing highlighted: arm pending, consume */
                evt.stopPropagation();
                escPending = true;
                escTimer = setTimeout(resetPending, 2000);
            } else {
                /* Second Esc: let event bubble → index.html closes the overlay */
                resetPending();
            }
        }, { capture: true, signal: this._abortController.signal });
    }

    /**
     * Sets up SVG save functionality
     */
    setupSaveSvg() {
        const saveSvgBtn = document.getElementById('save-svg');
        if (!saveSvgBtn) return;

        saveSvgBtn.addEventListener('click', () => {
            const svgElement = document.querySelector('#graph svg');
            if (!svgElement) return;
            const svgData = new XMLSerializer().serializeToString(svgElement);
            const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });
            const url = URL.createObjectURL(blob);
            const titleEl = document.getElementById("title");
            const filename = (titleEl ? titleEl.textContent.trim() : 'pipeline') + ".svg";
            const downloadLink = document.createElement("a");
            downloadLink.href = url;
            downloadLink.download = filename;
            document.body.appendChild(downloadLink);
            downloadLink.click();
            document.body.removeChild(downloadLink);
            URL.revokeObjectURL(url);
        });
    }

    /**
     * Processes SVG content with all managers
     * @param {Element} svg - SVG element
     */
    processSvgContent(svg) {
        this.pipelineNavigationManager.setupPipelineDotNavigation(svg);
        this.textEllipsizerManager.ellipsizeLongText(svg);
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = SvgOverlayManager;
} else {
    window.SvgOverlayManager = SvgOverlayManager;
}
