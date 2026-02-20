/*
 * d3-graphviz pipeline viewer.
 *
 * Renders GStreamer DOT files using d3-graphviz (Graphviz WASM + D3.js)
 * with interactive click handlers, detail panel, and zoom-to-cluster.
 */
const d3 = window.d3;

/* ── helpers ──────────────────────────────────────────────────────── */

function esc(s) {
  if (s == null) return '';
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function truncate(s, max) {
  if (!s || s.length <= max) return s;
  return s.slice(0, max - 1) + '\u2026';
}

/* ── PipelineViewer ───────────────────────────────────────────────── */

export class PipelineViewer {
  constructor({ containerEl, detailEl, infoEl, breadcrumbEl }) {
    this.containerEl = containerEl;
    this.detailEl = detailEl;
    this.infoEl = infoEl;
    this.breadcrumbEl = breadcrumbEl;
    this.dotContent = null;
    this.gv = null;
    this.selectedElement = null;

    /* Maps extracted from SVG after rendering */
    this.clusterDataMap = new Map();  // cluster title -> {type, name, state, params, element}
    this.nodeDataMap = new Map();     // node title -> {id, label, element}
    this.edgeDataMap = new Map();     // edge title -> {src, sink, label, element}
  }

  /* ── public API ──────────────────────────────────────────────────── */

  render(dotContent) {
    this.dotContent = dotContent;
    this.selectedElement = null;
    this._clearDetailPanel();

    this.gv = d3.select(this.containerEl).graphviz()
      .zoom(true)
      .fit(true)
      .on('end', () => this._onRenderEnd());

    this.gv.renderDot(dotContent);
  }

  /* ── render callback ────────────────────────────────────────────── */

  _onRenderEnd() {
    const svg = d3.select(this.containerEl).select('svg');
    if (svg.empty()) return;

    /* Make SVG fill container */
    svg.attr('width', '100%').attr('height', '100%');

    /* Extract element info from SVG */
    this._extractInfo();

    /* Render info panel */
    this._renderInfoPanel();

    /* Set up click handlers */
    this._setupClickHandlers();
  }

  /* ── extract info from Graphviz SVG ─────────────────────────────── */

  _extractInfo() {
    this.clusterDataMap.clear();
    this.nodeDataMap.clear();
    this.edgeDataMap.clear();

    const container = this.containerEl;

    /* Extract cluster (element/bin) info.
     * Graphviz SVG clusters have <title> + <text> children with:
     *   text[0] = type, text[1] = name, text[2] = state, text[3+] = params
     */
    const clusterMap = this.clusterDataMap;
    d3.select(container).selectAll('.cluster').each(function() {
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;

      const clusterId = titleEl.textContent;
      const texts = [];
      for (const child of this.children) {
        if (child.tagName === 'text') {
          texts.push(child.textContent);
        }
      }

      clusterMap.set(clusterId, {
        id: clusterId,
        type: texts[0] || '',
        name: texts[1] || '',
        state: texts[2] || '',
        params: texts.slice(3).join('\n'),
        element: this,
      });
    });

    /* Extract node (pad) info */
    const nodeMap = this.nodeDataMap;
    d3.select(container).selectAll('.node').each(function() {
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;

      const nodeId = titleEl.textContent;
      const texts = [];
      for (const child of this.querySelectorAll('text')) {
        texts.push(child.textContent);
      }

      nodeMap.set(nodeId, {
        id: nodeId,
        label: texts.join(' '),
        element: this,
      });
    });

    /* Extract edge info */
    const edgeMap = this.edgeDataMap;
    d3.select(container).selectAll('.edge').each(function() {
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;

      const edgeName = titleEl.textContent;
      const parts = edgeName.split('->');
      const texts = [];
      for (const child of this.querySelectorAll('text')) {
        texts.push(child.textContent);
      }

      edgeMap.set(edgeName, {
        id: edgeName,
        src: parts[0] ? parts[0].trim() : '',
        sink: parts[1] ? parts[1].trim() : '',
        label: texts.join('\n'),
        element: this,
      });
    });
  }

  /* ── click handlers ─────────────────────────────────────────────── */

  _setupClickHandlers() {
    const container = this.containerEl;
    const that = this;

    /* Click on cluster (element/bin) */
    d3.select(container).selectAll('.cluster').on('click', function(event) {
      event.stopPropagation();
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;
      that._selectCluster(titleEl.textContent, this);
    });

    /* Click on node (pad) */
    d3.select(container).selectAll('.node').on('click', function(event) {
      event.stopPropagation();
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;
      that._selectNode(titleEl.textContent, this);
    });

    /* Click on edge */
    d3.select(container).selectAll('.edge').on('click', function(event) {
      event.stopPropagation();
      const titleEl = this.querySelector(':scope > title');
      if (!titleEl) return;
      that._selectEdge(titleEl.textContent, this);
    });

    /* Click background to deselect */
    d3.select(container).select('svg').on('click', () => this._deselect());
  }

  /* ── selection ───────────────────────────────────────────────────── */

  _selectCluster(clusterId, element) {
    this.selectedElement = clusterId;
    this._clearHighlights();
    d3.select(element).classed('selected', true);
    this._highlightEdgesForCluster(element);

    const data = this.clusterDataMap.get(clusterId);
    if (data) this._showClusterDetail(data);
  }

  _selectNode(nodeId, element) {
    this.selectedElement = nodeId;
    this._clearHighlights();
    d3.select(element).classed('selected', true);
    this._highlightEdgesForNode(nodeId);

    const data = this.nodeDataMap.get(nodeId);
    if (data) this._showNodeDetail(data);
  }

  _selectEdge(edgeId, element) {
    this.selectedElement = edgeId;
    this._clearHighlights();
    d3.select(element).classed('selected', true);

    const data = this.edgeDataMap.get(edgeId);
    if (data) this._showEdgeDetail(data);
  }

  _deselect() {
    this.selectedElement = null;
    this._clearHighlights();
    this._clearDetailPanel();
  }

  _clearHighlights() {
    d3.select(this.containerEl).selectAll('.node, .edge, .cluster')
      .classed('selected', false);
  }

  _highlightEdgesForCluster(clusterEl) {
    /* Find all node IDs inside this cluster */
    const nodeIds = new Set();
    clusterEl.querySelectorAll('.node > title').forEach(t => {
      nodeIds.add(t.textContent);
    });

    /* Also include pads whose names start with the cluster element prefix.
     * GStreamer DOT uses cluster_<element_id> for cluster names and
     * <element_id>_<pad> for pad node names. */
    const titleEl = clusterEl.querySelector(':scope > title');
    if (titleEl) {
      const clusterId = titleEl.textContent;
      const prefix = clusterId.replace(/^cluster_/, '') + '_';
      for (const [nodeId] of this.nodeDataMap) {
        if (nodeId.startsWith(prefix)) nodeIds.add(nodeId);
      }
    }

    for (const [, edgeData] of this.edgeDataMap) {
      if (nodeIds.has(edgeData.src) || nodeIds.has(edgeData.sink)) {
        d3.select(edgeData.element).classed('selected', true);
      }
    }
  }

  _highlightEdgesForNode(nodeId) {
    for (const [, edgeData] of this.edgeDataMap) {
      if (edgeData.src === nodeId || edgeData.sink === nodeId) {
        d3.select(edgeData.element).classed('selected', true);
      }
    }
  }

  /* ── detail panel rendering ──────────────────────────────────────── */

  _clearDetailPanel() {
    if (this.detailEl)
      this.detailEl.innerHTML = '<div class="detail-empty">Click an element, pad, or edge to view details</div>';
  }

  _showClusterDetail(data) {
    if (!this.detailEl) return;

    let h = '<div class="detail-content">';
    h += `<h3 class="detail-title">${esc(data.name || data.id)}</h3>`;
    h += `<div class="detail-type">${esc(data.type)}</div>`;

    h += '<div class="detail-section"><h4>Properties</h4>';
    h += tbl([
      ['Type', data.type],
      ['Name', data.name],
      ['State', data.state || 'unknown'],
    ]);
    if (data.params) {
      h += `<h4>Parameters</h4><div class="caps-text">${esc(data.params)}</div>`;
    }
    h += '</div>';

    /* Find pads belonging to this element */
    const elementPrefix = data.id.replace(/^cluster_/, '') + '_';
    const elementPads = [];
    for (const [nodeId, nodeData] of this.nodeDataMap) {
      if (nodeId.startsWith(elementPrefix)) elementPads.push(nodeData);
    }

    if (elementPads.length) {
      h += `<div class="detail-section"><h4>Pads (${elementPads.length})</h4>`;
      h += tbl(elementPads.map(p => [p.id.replace(elementPrefix, ''), p.label]));
      h += '</div>';
    }

    /* Find connected edges */
    const padIds = new Set(elementPads.map(p => p.id));
    const connEdges = [];
    for (const [, ed] of this.edgeDataMap) {
      if (padIds.has(ed.src) || padIds.has(ed.sink)) connEdges.push(ed);
    }

    if (connEdges.length) {
      h += `<div class="detail-section"><h4>Connections (${connEdges.length})</h4>`;
      for (const e of connEdges) {
        h += `<div style="margin:.2em 0"><span style="color:var(--text-dim)">${esc(e.src)}</span> &rarr; <span style="color:var(--text-dim)">${esc(e.sink)}</span></div>`;
        if (e.label && e.label.trim()) {
          h += `<div class="caps-text">${esc(e.label.trim())}</div>`;
        }
      }
      h += '</div>';
    }

    /* Children (nested clusters) */
    const childClusters = [];
    if (data.element) {
      for (const child of data.element.children) {
        if (child.classList && child.classList.contains('cluster')) {
          const childTitle = child.querySelector(':scope > title');
          if (childTitle) {
            const childData = this.clusterDataMap.get(childTitle.textContent);
            if (childData) childClusters.push(childData);
          }
        }
      }
    }

    if (childClusters.length) {
      h += `<div class="detail-section"><h4>Children (${childClusters.length})</h4>`;
      h += tbl(childClusters.map(c => [c.name || c.id, c.type]));
      h += `<button class="drilldown-btn" id="drilldown-btn">Zoom into element</button>`;
      h += '</div>';
    }

    h += '</div>';
    this.detailEl.innerHTML = h;

    if (childClusters.length) {
      const btn = this.detailEl.querySelector('#drilldown-btn');
      if (btn) btn.addEventListener('click', () => this._zoomToCluster(data));
    }
  }

  _showNodeDetail(data) {
    if (!this.detailEl) return;

    let h = '<div class="detail-content">';
    h += `<h3 class="detail-title">${esc(data.id)}</h3>`;
    h += `<div class="detail-type">Pad</div>`;

    h += '<div class="detail-section"><h4>Properties</h4>';
    h += tbl([
      ['ID', data.id],
      ['Label', data.label],
    ]);
    h += '</div>';

    /* Connected edges */
    const connEdges = [];
    for (const [, ed] of this.edgeDataMap) {
      if (ed.src === data.id || ed.sink === data.id) connEdges.push(ed);
    }
    if (connEdges.length) {
      h += `<div class="detail-section"><h4>Connections</h4>`;
      for (const e of connEdges) {
        const peer = e.src === data.id ? e.sink : e.src;
        h += `<div style="margin:.2em 0">&rarr; <span style="color:var(--text-dim)">${esc(peer)}</span></div>`;
        if (e.label && e.label.trim()) {
          h += '<h4>Caps</h4>';
          h += `<div class="caps-text">${esc(e.label.trim())}</div>`;
        }
      }
      h += '</div>';
    }

    h += '</div>';
    this.detailEl.innerHTML = h;
  }

  _showEdgeDetail(data) {
    if (!this.detailEl) return;

    let h = '<div class="detail-content">';
    h += '<h3 class="detail-title">Edge</h3>';
    h += `<div class="detail-type">${esc(data.src)} &rarr; ${esc(data.sink)}</div>`;

    h += '<div class="detail-section"><h4>Properties</h4>';
    h += tbl([
      ['Source Pad', data.src],
      ['Sink Pad', data.sink],
    ]);
    h += '</div>';

    if (data.label && data.label.trim()) {
      h += '<div class="detail-section"><h4>Caps</h4>';
      h += `<div class="caps-text">${esc(data.label.trim())}</div>`;
      h += '</div>';
    }

    h += '</div>';
    this.detailEl.innerHTML = h;
  }

  /* ── info sidebar ────────────────────────────────────────────────── */

  _renderInfoPanel() {
    if (!this.infoEl) return;
    let h = '';

    h += '<div class="info-section"><h3>Pipeline</h3>';
    h += `<div class="info-item"><span class="info-label">Elements:</span> ${this.clusterDataMap.size}</div>`;
    h += `<div class="info-item"><span class="info-label">Pads:</span> ${this.nodeDataMap.size}</div>`;
    h += `<div class="info-item"><span class="info-label">Edges:</span> ${this.edgeDataMap.size}</div>`;
    h += '</div>';

    /* Top-level elements (clusters whose parent is the root graph group) */
    const topLevel = [];
    const svg = d3.select(this.containerEl).select('svg');
    const topG = svg.select('g').node();
    if (topG) {
      for (const child of topG.children) {
        if (child.classList && child.classList.contains('cluster')) {
          const titleEl = child.querySelector(':scope > title');
          if (titleEl) {
            const data = this.clusterDataMap.get(titleEl.textContent);
            if (data) topLevel.push(data);
          }
        }
      }
    }

    if (topLevel.length) {
      h += '<div class="info-section"><h3>Top-Level Elements</h3>';
      for (const el of topLevel) {
        h += `<div class="info-item"><span class="info-label">${esc(el.type)}:</span> ${esc(el.name)}</div>`;
      }
      h += '</div>';
    }

    this.infoEl.innerHTML = h;
  }

  /* ── zoom to cluster ────────────────────────────────────────────── */

  _zoomToCluster(data) {
    const el = data.element;
    if (!el) return;

    const svg = d3.select(this.containerEl).select('svg');
    const bbox = el.getBBox();
    const containerRect = this.containerEl.getBoundingClientRect();
    const W = containerRect.width;
    const H = containerRect.height;

    const padding = 40;
    const scale = Math.min(
      W / (bbox.width + padding * 2),
      H / (bbox.height + padding * 2)
    );
    const tx = W / 2 - (bbox.x + bbox.width / 2) * scale;
    const ty = H / 2 - (bbox.y + bbox.height / 2) * scale;

    const transform = d3.zoomIdentity.translate(tx, ty).scale(scale);

    /* Use d3-graphviz's zoom behavior if available, otherwise create one */
    try {
      const zoomBehavior = this.gv.zoomBehavior();
      const zoomSelection = this.gv.zoomSelection();
      zoomSelection.transition()
        .duration(750)
        .call(zoomBehavior.transform, transform);
    } catch (e) {
      /* Fallback: apply transform directly to the main group */
      svg.select('g').transition()
        .duration(750)
        .attr('transform', transform.toString());
    }
  }
}

/* ── detail table helper ──────────────────────────────────────────── */

function tbl(rows) {
  let h = '<table class="detail-table">';
  for (const [k, v] of rows)
    h += `<tr><td>${esc(k)}</td><td>${esc(v)}</td></tr>`;
  return h + '</table>';
}
