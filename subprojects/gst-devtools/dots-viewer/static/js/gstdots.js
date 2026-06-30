let ws = null;
let pendingObservers = [];
let observerDebounceTimer = null;
const OBSERVER_DEBOUNCE_MS = 300;
let activeResultIndex = -1;

function activatePendingObservers() {
    for (const {observer, target} of pendingObservers) {
        observer.observe(target);
    }
    pendingObservers = [];
}

function scheduleObserverActivation() {
    if (observerDebounceTimer) {
        clearTimeout(observerDebounceTimer);
    }
    observerDebounceTimer = setTimeout(activatePendingObservers, OBSERVER_DEBOUNCE_MS);
}

let graphvizPromise = null;

/** Loads (once) and returns the shared graphviz wasm instance. */
function getGraphviz() {
    if (!graphvizPromise) {
        graphvizPromise = window.Graphviz.load();
    }
    return graphvizPromise;
}

/**
 * Splits a GStreamer dot label into its type and object name.
 *
 * GstDebug encodes both pipeline and element labels as
 *   "<TypeName>\n<object-name>\n[state]...".
 * This line convention is the one piece intrinsic to GStreamer's dump format;
 * the graph structure itself is read from graphviz, not parsed by hand.
 */
function labelTypeName(label) {
    const parts = (label || '').split('\\n');
    return {
        type: (parts[0] || '').replace(/[<>]/g, '').trim(),
        name: (parts[1] || '').trim(),
    };
}

/**
 * Builds the search index for one .dot file by parsing it with graphviz
 * (the same wasm used to render the overlay) rather than scraping the text.
 *
 * Each element/bin is a `cluster_node_<name>_<ptr>` subgraph whose name
 * matches the SVG cluster <title>, so it can be used to zoom straight to that
 * element. The top-level pipeline is the graph's own label.
 *
 * The `nop2` engine skips layout, so this is cheap (a few ms) and never lays
 * the graph out twice.
 *
 * @param {string} content - Raw .dot file content
 * @returns {Promise<{elements: Array, pipeline: ({name,type}|null)}>}
 */
async function buildSearchIndex(content) {
    const empty = { elements: [], pipeline: null };
    if (!content) return empty;

    let graph;
    try {
        const gv = await getGraphviz();
        graph = JSON.parse(gv.layout(content, 'json', 'nop2'));
    } catch (e) {
        console.warn('dots search: could not parse graph for indexing', e);
        return empty;
    }

    const elements = [];
    for (const obj of (graph.objects || [])) {
        // Element/bin clusters carry a label; pad-group (_sink/_src) clusters
        // have an empty one and are skipped.
        if (!obj.name || !obj.name.startsWith('cluster_node_') || !obj.label) {
            continue;
        }
        const { type, name } = labelTypeName(obj.label);
        if (!name) continue;
        elements.push({ clusterId: obj.name, name, type });
    }

    const { type, name } = labelTypeName(graph.label);
    const pipeline = name ? { name, type } : null;

    return { elements, pipeline };
}

function createOverlayElement(dot_info, fname, focusClusterId) {
    let overlay = document.getElementById("overlay");
    if (overlay) {
        console.warn('Overlay already exists');
        return;
    }

    const overlayDiv = document.createElement('div');
    overlayDiv.id = "overlay";
    overlayDiv.className = 'overlay';

    // Header with title and close button
    const header = document.createElement('div');
    header.className = 'overlay-header';

    const title = document.createElement('h2');
    title.id = 'title';
    title.textContent = fname.replace('.dot', '');
    header.appendChild(title);

    const closeButton = document.createElement('a');
    closeButton.className = 'closebtn';
    closeButton.innerHTML = '&times;';
    closeButton.onclick = (event) => {
        removePipelineOverlay();
        event.stopPropagation();
    };
    header.appendChild(closeButton);
    overlayDiv.appendChild(header);

    // Content area with graph
    const contentDiv = document.createElement('div');
    contentDiv.className = 'overlay-content';

    const graphDiv = document.createElement('div');
    graphDiv.id = 'graph';
    graphDiv.style.width = '100%';
    graphDiv.style.height = '100%';
    contentDiv.appendChild(graphDiv);

    overlayDiv.appendChild(contentDiv);

    // Instructions
    const instructions = document.createElement('div');
    instructions.className = 'overlay-instructions';
    instructions.innerHTML = 'Click node to highlight | Esc to unhighlight | Esc×2 to close | Ctrl+Scroll to zoom | Scroll to pan | Double-click text to copy';
    overlayDiv.appendChild(instructions);

    // Actions
    const actions = document.createElement('div');
    actions.className = 'overlay-actions';
    const saveBtn = document.createElement('button');
    saveBtn.id = 'save-svg';
    saveBtn.textContent = 'Save SVG';
    actions.appendChild(saveBtn);
    overlayDiv.appendChild(actions);

    document.body.appendChild(overlayDiv);

    // Render with d3-graphviz via SvgOverlayManager
    const manager = new window.SvgOverlayManager();
    overlayDiv._svgManager = manager;
    manager.init(dot_info.content, fname, focusClusterId);

    return overlayDiv;
}

function dotId(dot_info) {
    return `${dot_info.name}`;
}

function generateSvg(container) {
    if (container.rendered) {
        return;
    }
    container.rendered = true;

    try {
        d3.select(container).graphviz()
            .zoom(false)
            .fit(true)
            .renderDot(container.dot_info.content);
    } catch (error) {
        console.error(`Failed rendering SVG: ${error}`, error);
    }
}

async function createNewDotDiv(pipelines_div, dot_info) {
    let path = dot_info.name;
    let dirname = path.split('/');
    let parent_div = pipelines_div;
    if (dirname.length > 1) {
        dirname = `/${dirname.slice(0, -1).join('/')}/`;
    } else {
        dirname = "/";
    }
    let div_id = `content-${dirname}`;
    parent_div = document.getElementById(div_id);

    if (!parent_div) {
        parent_div = document.createElement("div");
        parent_div.id = `dir-${dirname}`;
        parent_div.className = "wrap-collabsible";
        parent_div.innerHTML = `<input id="collapsible-${dirname}" class="toggle" type="checkbox">
                              <label for="collapsible-${dirname}" class="lbl-toggle">${dirname}</label>
                              <div class="collapsible-content" id="content-${dirname}">
                              </div>`;

        if (pipelines_div.firstChild) {
            pipelines_div.insertBefore(parent_div, pipelines_div.firstChild);
        } else {
            pipelines_div.appendChild(parent_div);
        }

        parent_div = document.getElementById(`content-${dirname}`);
    }


    let div = document.createElement("div");
    div.id = dotId(dot_info);
    div.className = "content-inner pipelineDiv";
    div.setAttribute("data_score", "0");
    div.setAttribute("creation_time", dot_info.creation_time);

    let titleEl = document.createElement("h2");
    titleEl.textContent = dot_info.name.replace(".dot", "");

    let previewDiv = document.createElement("div");
    previewDiv.className = "preview";
    previewDiv.dot_info = dot_info;

    // Index the graph for search asynchronously so the websocket handler
    // isn't blocked; it's ready well before the user opens the search palette.
    div._elements = [];
    div._pipeline = null;
    div._indexed = buildSearchIndex(dot_info.content).then((idx) => {
        div._elements = idx.elements;
        div._pipeline = idx.pipeline;
    });

    const observer = new IntersectionObserver((entries, observer) => {
        for (const entry of entries) {
            if (entry.isIntersecting) {
                console.debug(`Preview ${div.id} is visible`);
                generateSvg(previewDiv);
                observer.unobserve(entry.target);
            }
        }
    }, {
        rootMargin: '100px'
    });
    pendingObservers.push({observer, target: previewDiv});
    scheduleObserverActivation();

    div.appendChild(titleEl);
    div.appendChild(previewDiv);

    div.onclick = function () {
        unsetUrlVariable('element');
        setUrlVariable('pipeline', div.id);
        createOverlayElement(previewDiv.dot_info, dot_info.name);
    };


    if (parent_div.firstChild) {
        parent_div.insertBefore(div, parent_div.firstChild);
    } else {
        parent_div.appendChild(div);
    }

    updateSearch();
    updateFromUrl(false);
}

function unsetUrlVariable(key) {
    let url = new URL(window.location.href);
    let searchParams = new URLSearchParams(url.search);
    searchParams.delete(key);
    url.search = searchParams.toString();
    window.history.pushState({}, '', url);
}

function setUrlVariable(key, value) {
    let url = new URL(window.location.href);
    let searchParams = new URLSearchParams(url.search);
    searchParams.set(key, value);
    url.search = searchParams.toString();

    window.history.pushState({}, '', url);
}

export function updateFromUrl(noHistoryUpdate) {
    if (window.location.search) {
        const url = new URL(window.location.href);
        const pipeline = url.searchParams.get('pipeline');
        const element = url.searchParams.get('element');
        if (pipeline) {
            console.log(`Opening overlay for ${pipeline}`);
            let div = document.getElementById(pipeline);
            if (!div) {
                console.info(`Pipeline ${pipeline} not found`);
                return;
            }
            let previewDiv = div.querySelector('.preview');
            if (previewDiv && previewDiv.dot_info) {
                createOverlayElement(previewDiv.dot_info, pipeline, element);
            }
        }
    }
}

export function connectWs() {
    console.assert(ws === null, "Websocket already exists");

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.hostname;
    const port = window.location.port;
    const wsUrl = `${protocol}//${host}:${port}/ws/`;
    let pipelines_div = document.getElementById("pipelines");

    ws = new WebSocket(wsUrl);
    console.info(`Websocklet ${ws} created with ${wsUrl}`);
    ws.onopen = () => {
        let pipelines_div = document.getElementById("pipelines");

        console.log(`WebSocket connected, removing all children from ${pipelines_div}`);
        while (pipelines_div.firstChild) {
            console.debug(`Removing ${pipelines_div.firstChild}`);
            pipelines_div.removeChild(pipelines_div.firstChild);
        }
    };

    ws.onmessage = function (event) {
        console.debug(`Received message: ${event.data}`)
        try {
            const obj = JSON.parse(event.data);
            if (obj.type == "NewDot") {
                if (document.getElementById(dotId(obj))) {
                    console.warn(`Pipeline ${obj.name} already exists`);
                    return;
                }
                createNewDotDiv(pipelines_div, obj, ws).then(() => {
                    updateSearch();
                });
            } else if (obj.type == "DotRemoved") {
                console.debug(`Trying to remove: ${obj.name}`);
                let dot_id = dotId(obj);
                let dot_div = document.getElementById(dot_id);
                if (dot_div) {
                    let parent = dot_div.parentElement;
                    if (obj.name.includes('/') && parent.childElementCount == 1) {
                        console.info(`Last element in folder, removing parent div for ${dot_id}`);
                        const wrapperDiv = parent.closest('.wrap-collabsible');
                        if (wrapperDiv) {
                            wrapperDiv.remove();
                        }
                    } else {
                        console.info(`Removing dot_div ${dot_id}`);
                        dot_div.remove();
                    }
                } else {
                    console.error(`dot_div ${dot_id} not found`);
                }

                updateSearch();
            } else {
                console.warn(`Unknown message type: ${obj.type}`);
            }
        } catch (e) {
            console.error(`Error: ${e}`);

            throw e;
        }
    }

    ws.onclose = (event) => {
        console.log('WebSocket disconnected', event.reason);
        ws.close();
        ws = null;
        setTimeout(connectWs, 10000);
    };

}

function updateSearch() {
    const input = document.getElementById('search');
    const allDivs = document.querySelectorAll('.pipelineDiv');

    if (document.querySelectorAll('.toggle').length == 1) {
        // If the is only 1 folder,  expand  it
        let toggle = document.querySelector('.toggle')
        if (toggle && !toggle.auto_checked) {
            toggle.checked = true;
            toggle.auto_checked = true;
        }
    }

    if (input.value === "") {
        let divs = Array.from(allDivs).map(div => {
            div.style.display = '';
            div.setAttribute("data_score", "0");

            return div;
        });

        divs.sort((a, b) => {
            const scoreA = parseInt(a.getAttribute('creation_time'), 0);
            const scoreB = parseInt(b.getAttribute('creation_time'), 0);

            console.debug('Comparing', scoreA, scoreB, " = ", scoreB - scoreA);
            return scoreB - scoreA;
        });

        for (const div of divs) {
            console.debug(`Moving ${div.id} in {div.parentNode}}`);
            div.parentNode.appendChild(div);
        }

        return;
    }

    const options = {

        includeScore: true,
        threshold: 0.6,
        keys: ['title']
    };


    const list = Array.from(allDivs).map(div => ({
        id: div.getAttribute('id'), // Assuming each div has an ID
        title: div.querySelector('h2').textContent
    }));

    const fuse = new window.Fuse(list, options);
    const results = fuse.search(input.value);

    for (let div of allDivs) {
        div.style.display = 'none';
    }

    let divs = results.map(result => {
        let div = document.getElementById(result.item.id);
        div.style.display = '';
        div.setAttribute('data_score', result.score);

        return div;
    });

    divs.sort((a, b) => {
        const scoreA = parseFloat(a.getAttribute('data_score'), 0);
        const scoreB = parseFloat(b.getAttribute('data_score'), 0);

        return scoreA - scoreB; // For ascending order. Use (scoreB - scoreA) for descending.
    });

    for (let div of divs) {
        div.parentNode.appendChild(div);
    }
}

/**
 * Opens the pipeline overlay for `pipelineId` and zooms to `clusterId`.
 */
function openElement(pipelineId, clusterId) {
    const div = document.getElementById(pipelineId);
    if (!div) {
        console.warn(`Pipeline ${pipelineId} not found`);
        return;
    }
    const previewDiv = div.querySelector('.preview');
    if (!previewDiv || !previewDiv.dot_info) return;

    closeSearchPalette();
    removePipelineOverlay(true);
    setUrlVariable('pipeline', pipelineId);
    if (clusterId) {
        setUrlVariable('element', clusterId);
    } else {
        unsetUrlVariable('element');
    }
    createOverlayElement(previewDiv.dot_info, previewDiv.dot_info.name, clusterId || undefined);
}

/* ── Search palette (Ctrl+K) ──────────────────────────────────────────
 * The search lives in a fixed, top-layer overlay so it can be invoked and
 * used even while a pipeline graph overlay is open. */

export function isSearchPaletteOpen() {
    const palette = document.getElementById('search-palette');
    return !!(palette && palette.classList.contains('open'));
}

export function openSearchPalette() {
    const palette = document.getElementById('search-palette');
    if (!palette) return;
    palette.classList.add('open');
    const input = document.getElementById('search');
    input.focus();
    input.select();
    updateElementSearch(input.value.trim());
}

export function closeSearchPalette() {
    const palette = document.getElementById('search-palette');
    if (!palette || !palette.classList.contains('open')) return;
    palette.classList.remove('open');
    const input = document.getElementById('search');
    input.value = '';
    input.blur();
    clearElementResults();
    updateSearch();
}

export function toggleSearchPalette() {
    if (isSearchPaletteOpen()) {
        closeSearchPalette();
    } else {
        openSearchPalette();
    }
}

/**
 * Returns the div id of the pipeline currently shown in the overlay, or null
 * if no overlay is open. The open pipeline is tracked in the URL.
 */
function currentOverlayPipelineId() {
    if (!document.getElementById('overlay')) return null;
    return new URL(window.location.href).searchParams.get('pipeline');
}

function clearElementResults() {
    const results = document.getElementById('search-results');
    if (results) {
        results.innerHTML = '';
        results.style.display = 'none';
    }
    activeResultIndex = -1;
}

/**
 * Highlights the result row at `idx` (with wrap-around) and scrolls it into
 * view. Keeps `activeResultIndex` in sync for Enter/click selection.
 */
function setActiveResult(idx) {
    const rows = document.querySelectorAll('#search-results .search-result');
    if (rows.length === 0) {
        activeResultIndex = -1;
        return;
    }
    if (idx < 0) idx = rows.length - 1;
    if (idx >= rows.length) idx = 0;
    activeResultIndex = idx;
    rows.forEach((r, i) => r.classList.toggle('active', i === idx));
    rows[idx].scrollIntoView({ block: 'nearest' });
}

/**
 * Builds a flat searchable list of every element across all loaded pipelines.
 */
async function collectElements() {
    const divs = Array.from(document.querySelectorAll('.pipelineDiv'));
    await Promise.all(divs.map(div => div._indexed).filter(Boolean));

    const list = [];
    divs.forEach(div => {
        const pipelineName = div.querySelector('h2').textContent;

        // The pipeline (dot file) itself is searchable by its name, type and
        // dump file name, and is rendered before the elements it contains.
        if (div._pipeline) {
            list.push({
                kind: 'pipeline',
                pipelineId: div.id,
                pipelineName,
                clusterId: null,
                name: div._pipeline.name,
                type: div._pipeline.type,
                file: pipelineName,
            });
        }

        if (div._elements) {
            for (const el of div._elements) {
                list.push({
                    kind: 'element',
                    pipelineId: div.id,
                    pipelineName,
                    clusterId: el.clusterId,
                    name: el.name,
                    type: el.type,
                });
            }
        }
    });
    return list;
}

let searchSeq = 0;

/**
 * Fuzzy-matches the query against pipelines and elements across all loaded
 * dot files and renders a results dropdown. Selecting a result opens the right
 * pipeline and (for elements) zooms to it.
 *
 * Async because the search index is parsed lazily by graphviz; a sequence
 * guard discards results from a query the user has already typed past.
 */
async function updateElementSearch(query) {
    const results = document.getElementById('search-results');
    if (!results) return;

    if (!query) {
        clearElementResults();
        return;
    }

    const seq = ++searchSeq;
    const list = await collectElements();
    if (seq !== searchSeq) return; // a newer query superseded this one
    const fuse = new window.Fuse(list, {
        includeScore: true,
        // Keep this tight: every pipeline shares the "GstPipeline" type, so a
        // loose threshold makes unrelated queries (e.g. "GESTimeline") fuzzily
        // match it and surface every pipeline. 0.2 still allows real typos.
        threshold: 0.2,
        ignoreLocation: true,
        keys: ['name', 'type', 'file'],
    });
    /* Ordering: results from the pipeline currently shown in the overlay come
     * first, then pipelines (dot files) before elements; each group keeps its
     * own relevance order. */
    const current = currentOverlayPipelineId();
    const matches = fuse.search(query).sort((a, b) => {
        const ca = (current && a.item.pipelineId === current) ? 0 : 1;
        const cb = (current && b.item.pipelineId === current) ? 0 : 1;
        const ka = a.item.kind === 'pipeline' ? 0 : 1;
        const kb = b.item.kind === 'pipeline' ? 0 : 1;
        return ca - cb || ka - kb || a.score - b.score;
    }).slice(0, 50);

    results.innerHTML = '';
    if (matches.length === 0) {
        results.style.display = 'none';
        return;
    }

    for (const { item } of matches) {
        const row = document.createElement('div');
        row.className = 'search-result'
            + (item.kind === 'pipeline' ? ' is-pipeline' : '')
            + (current && item.pipelineId === current ? ' in-current-pipeline' : '');
        row.innerHTML =
            `<span class="sr-name"></span>` +
            `<span class="sr-type"></span>` +
            `<span class="sr-pipeline"></span>`;
        row.querySelector('.sr-name').textContent = item.name;
        row.querySelector('.sr-type').textContent = item.type;
        row.querySelector('.sr-pipeline').textContent = item.pipelineName.replace('.dot', '');
        row.addEventListener('mousedown', (e) => {
            // mousedown (not click) so it fires before the input blur hides us
            e.preventDefault();
            clearElementResults();
            openElement(item.pipelineId, item.clusterId);
        });
        row.addEventListener('mouseenter', () => {
            setActiveResult(Array.prototype.indexOf.call(results.children, row));
        });
        results.appendChild(row);
    }
    results.style.display = 'block';
    setActiveResult(0);
}

export function connectSearch() {
    const input = document.getElementById('search');
    input.addEventListener('input', function () {
        updateSearch();
        updateElementSearch(input.value.trim());
    });

    input.addEventListener('keydown', function (e) {
        const rows = document.querySelectorAll('#search-results .search-result');
        if (e.key === 'ArrowDown') {
            if (rows.length) {
                e.preventDefault();
                setActiveResult(activeResultIndex + 1);
            }
        } else if (e.key === 'ArrowUp') {
            if (rows.length) {
                e.preventDefault();
                setActiveResult(activeResultIndex - 1);
            }
        } else if (e.key === 'Enter') {
            const target = rows[activeResultIndex] || rows[0];
            if (target) {
                e.preventDefault();
                target.dispatchEvent(new MouseEvent('mousedown'));
            }
        }
    });

    /* Click on the palette backdrop (outside the box) closes it. */
    const palette = document.getElementById('search-palette');
    if (palette) {
        palette.addEventListener('mousedown', function (e) {
            if (e.target === palette) closeSearchPalette();
        });
    }

    /* Ctrl/Cmd+K toggles the palette; "/" opens it when not already typing. */
    document.addEventListener('keydown', function (e) {
        if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) {
            e.preventDefault();
            toggleSearchPalette();
        } else if (e.key === '/' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'TEXTAREA') {
            e.preventDefault();
            openSearchPalette();
        }
    });

    /* Escape closes the palette first, before the pipeline-overlay handlers.
     * Capture phase + early registration ensures we run before the overlay's
     * own Escape handling, so closing the palette never closes the graph. */
    document.addEventListener('keyup', function (e) {
        if (e.key === 'Escape' && isSearchPaletteOpen()) {
            e.stopImmediatePropagation();
            closeSearchPalette();
        }
    }, { capture: true });
}

export function removePipelineOverlay(noHistoryUpdate) {
    let overlay = document.getElementById("overlay");
    if (!overlay) {
        return;
    }
    if (overlay._svgManager) {
        overlay._svgManager.destroy();
    }
    overlay.parentNode.removeChild(overlay);
    if (!noHistoryUpdate) {
        unsetUrlVariable('pipeline');
        unsetUrlVariable('element');
    }
    updateSearch();
}

export function dumpPipelines() {
    if (ws) {
        ws.send(JSON.stringify({ type: "Snapshot" }));
    }
}

