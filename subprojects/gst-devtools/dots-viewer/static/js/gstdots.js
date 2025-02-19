import { instance } from "/js/viz-standalone.mjs";
import Fuse from '/js/fuse.min.mjs'

let ws = null;

async function createOverlayElement(img, fname) {
    let overlay = document.getElementById("overlay");
    if (overlay || img.creating_svg) {
        console.warn(`Overlay already exists`);
        return;
    }

    const overlayDiv = document.createElement('div');
    overlayDiv.id = "overlay";
    overlayDiv.className = 'overlay';

    document.getElementById('pipelines').appendChild(overlayDiv);
    await generateSvg(img);

    const closeButton = document.createElement('a');
    closeButton.href = 'javascript:void(0)';
    closeButton.className = 'closebtn';
    closeButton.innerHTML = '&times;';
    closeButton.onclick = (event) => {
        removePipelineOverlay();

        event.stopPropagation();
    }
    overlayDiv.appendChild(closeButton);

    const contentDiv = document.createElement('div');
    contentDiv.className = 'overlay-content';

    const iframe = document.createElement('iframe');
    iframe.loading = 'lazy';
    iframe.src = '/overlay.html?svg=' + img.src + '&title=' + fname;
    iframe.className = 'internalframe';
    contentDiv.appendChild(iframe);

    overlayDiv.appendChild(contentDiv);

    return overlayDiv;
}

function dotId(dot_info) {
    return `${dot_info.name}`;
}

async function generateSvg(img) {
    if (img.src != "" || img.creating_svg) {
        console.debug('Image already generated');
        return;
    }

    img.creating_svg = true;
    try {
        let viz = await instance();
        const svg = viz.renderSVGElement(img.dot_info.content);
        img.src = URL.createObjectURL(new Blob([svg.outerHTML], { type: 'image/svg+xml' }));
        img.creating_svg = false;
    } catch (error) {
        console.error(`Fail rendering SVG for '${img.dot_info.content}' failed: ${error}`, error);
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

    let title = document.createElement("h2");
    title.textContent = dot_info.name.replace(".dot", "");

    let img = document.createElement("img");
    img.alt = "image";
    img.className = "preview";
    img.loading = "lazy";
    img.dot_info = dot_info;
    const observer = new IntersectionObserver((entries, observer) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                console.debug(`Image ${div.id} is visible`);
                generateSvg(img);
                observer.unobserve(entry.target);
            }
        });
    }, {
        rootMargin: '100px'
    });
    observer.observe(img);

    div.appendChild(title);
    div.appendChild(img);

    div.onclick = function () {
        createOverlayElement(img, title.textContent).then(_ => {
            setUrlVariable('pipeline', div.id);
        });
    }


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
        if (pipeline) {
            console.log(`Creating overlay for ${pipeline}`);
            let div = document.getElementById(pipeline);
            if (!div) {
                console.info(`Pipeline ${pipeline} not found`);
                return;
            }
            let img = div.querySelector('img');
            let title = div.querySelector('h2');
            createOverlayElement(img, title.textContent).then(_ => {
                console.debug(`Overlay created for ${pipeline}`);
            });
        }
    } else {
        removePipelineOverlay(noHistoryUpdate);
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

        divs.forEach(div => {
            console.debug(`Moving ${div.id} in {div.parentNode}}`);
            div.parentNode.appendChild(div);
        });

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

    const fuse = new Fuse(list, options);
    const results = fuse.search(input.value);

    allDivs.forEach(div => div.style.display = 'none');
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

    divs.forEach(div => {
        div.parentNode.appendChild(div);
    });
}

export function connectSearch() {
    const input = document.getElementById('search');
    input.addEventListener('input', function () {
        updateSearch();

    });
}

export function removePipelineOverlay(noHistoryUpdate) {
    let overlay = document.getElementById("overlay");
    if (!overlay) {
        return;
    }
    overlay.parentNode.removeChild(overlay);
    if (!noHistoryUpdate) {
        unsetUrlVariable('pipeline');
    }
    updateSearch();
}

export function dumpPipelines() {
    if (ws) {
        ws.send(JSON.stringify({ type: "Snapshot" }));
    }
}

