const $ = (id) => document.getElementById(id);

const state = {
  browseNodeId: "ns=0;i=85",
  browseStack: [{ label: "Objects", id: "ns=0;i=85" }],
};

/** Wire type codes — same as PROTOCOL.md */
const TYPE_OPTIONS = [
  { value: 0, label: "Boolean" },
  { value: 1, label: "Int32" },
  { value: 2, label: "Int64" },
  { value: 3, label: "Float32" },
  { value: 4, label: "Float64" },
  { value: 5, label: "String" },
];

async function api(path, opts = {}) {
  const r = await fetch(path, {
    headers: { "Content-Type": "application/json", ...opts.headers },
    ...opts,
  });
  const text = await r.text();
  let data;
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    data = { error: text };
  }
  if (!r.ok) throw new Error(data.error || r.statusText);
  return data;
}

function setStatus(msg, isErr) {
  const el = $("statusLine");
  el.textContent = msg;
  el.className = "status" + (isErr ? " err" : "");
}

async function pollStatus() {
  try {
    const s = await api("/api/status");
    const parts = [];
    parts.push(s.opcuaConnected ? "OPC UA: connected" : "OPC UA: disconnected");
    if (s.namespaceIndex) parts.push(`ns index ${s.namespaceIndex}`);
    parts.push(s.bridgeRunning ? `Bridge: running on :${s.tcpPort}` : "Bridge: stopped");
    parts.push(`TCP clients: ${s.clientCount}`);
    if (s.lastError) parts.push(`Last error: ${s.lastError}`);
    setStatus(parts.join(" · "));
    $("btnConnect").disabled = s.bridgeRunning || s.opcuaConnected;
    $("btnDisconnect").disabled = s.bridgeRunning || !s.opcuaConnected;
    $("btnStart").disabled = !s.opcuaConnected || s.bridgeRunning;
    $("btnStop").disabled = !s.bridgeRunning;
    $("endpointUrl").disabled = s.bridgeRunning;
    $("namespaceUri").disabled = s.bridgeRunning;
    $("tcpPort").disabled = s.bridgeRunning;
  } catch (e) {
    setStatus("Status unavailable: " + e.message, true);
  }
}

function loadFormFromConfig(c) {
  $("endpointUrl").value = c.endpointUrl || "";
  $("namespaceUri").value = c.namespaceUri || "";
  $("tcpPort").value = c.tcpPort || 9000;
  renderMappings(c.mappings || []);
}

function gatherConfig() {
  const mappings = [];
  $("mapBody").querySelectorAll("tr").forEach((tr) => {
    const cb = tr.querySelector('input[type="checkbox"]');
    const cells = tr.querySelectorAll("td");
    mappings.push({
      enabled: cb.checked,
      messagePackId: cells[1].querySelector("input").value.trim(),
      nodeIdString: cells[2].querySelector("input").value.trim(),
      typeCode: parseInt(cells[3].querySelector("select").value, 10),
    });
  });
  return {
    endpointUrl: $("endpointUrl").value.trim(),
    namespaceUri: $("namespaceUri").value.trim(),
    tcpPort: parseInt($("tcpPort").value, 10) || 9000,
    mappings,
  };
}

function renderMappings(mappings) {
  const tb = $("mapBody");
  tb.innerHTML = "";
  mappings.forEach((m) => addMappingRow(m));
  if (mappings.length === 0) addMappingRow({});
}

function addMappingRow(m = {}) {
  const tr = document.createElement("tr");
  const raw = m.typeCode;
  const tc =
    raw === undefined || raw === "" || Number.isNaN(Number(raw))
      ? 0
      : Number(raw);
  const typeOpts = TYPE_OPTIONS.map(
    (opt) =>
      `<option value="${opt.value}" ${
        tc === opt.value ? "selected" : ""
      }>${opt.label}</option>`
  ).join("");
  tr.innerHTML = `
    <td><input type="checkbox" ${m.enabled !== false ? "checked" : ""} /></td>
    <td><input type="text" value="${escapeAttr(m.messagePackId || "")}" placeholder="Sensor1" /></td>
    <td><input type="text" value="${escapeAttr(m.nodeIdString || "")}" placeholder="ns=2;i=123" /></td>
    <td><select>${typeOpts}</select></td>
    <td><button type="button" class="linkbtn rm">Remove</button></td>
  `;
  tr.querySelector(".rm").onclick = () => tr.remove();
  $("mapBody").appendChild(tr);
}

function escapeAttr(s) {
  return s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
}

function renderBreadcrumb() {
  const el = $("breadcrumb");
  el.textContent = "";
  if (state.browseStack.length > 1) {
    const up = document.createElement("button");
    up.type = "button";
    up.className = "linkbtn";
    up.textContent = "↑ Up";
    up.onclick = () => {
      state.browseStack.pop();
      const parent = state.browseStack[state.browseStack.length - 1];
      state.browseNodeId = parent.id;
      loadBrowse();
    };
    el.appendChild(up);
    el.appendChild(document.createTextNode(" "));
  }
  el.appendChild(
    document.createTextNode(
      state.browseStack.map((s) => s.label + " (" + s.id + ")").join(" → ")
    )
  );
}

async function loadBrowse() {
  renderBreadcrumb();
  const tb = $("browseBody");
  try {
    const data = await api("/api/browse", {
      method: "POST",
      body: JSON.stringify({ nodeId: state.browseNodeId }),
    });
    tb.innerHTML = "";
    for (const ref of data.references || []) {
      const tr = document.createElement("tr");
      const cls =
        ref.nodeClass === 1 ? "Object" : ref.nodeClass === 2 ? "Variable" : String(ref.nodeClass);
      tr.innerHTML = `
      <td>${escapeHtml(ref.displayName || ref.browseName)}</td>
      <td>${cls}</td>
      <td><code>${escapeHtml(ref.nodeId)}</code></td>
      <td></td>
    `;
      const actions = tr.querySelector("td:last-child");
      if (ref.nodeClass === 1) {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "linkbtn";
        b.textContent = "Open";
        b.onclick = (e) => {
          e.stopPropagation();
          state.browseStack.push({
            label: ref.displayName || ref.browseName,
            id: ref.nodeId,
          });
          state.browseNodeId = ref.nodeId;
          loadBrowse();
        };
        actions.appendChild(b);
      } else if (ref.nodeClass === 2) {
        const b = document.createElement("button");
        b.type = "button";
        b.className = "linkbtn";
        b.textContent = "Add sensor";
        b.onclick = (e) => {
          e.stopPropagation();
          addMappingRow({
            enabled: true,
            messagePackId: ref.browseName || "sensor",
            nodeIdString: ref.nodeId,
            typeCode: 0,
          });
        };
        actions.appendChild(b);
      }
      tr.onclick = () => {
        if (ref.nodeClass === 1) {
          state.browseStack.push({
            label: ref.displayName || ref.browseName,
            id: ref.nodeId,
          });
          state.browseNodeId = ref.nodeId;
          loadBrowse();
        }
      };
      tb.appendChild(tr);
    }
  } catch (e) {
    tb.innerHTML = `<tr><td colspan="4">${escapeHtml(e.message)}</td></tr>`;
  }
}

function resetBrowseToRoot() {
  state.browseNodeId = "ns=0;i=85";
  state.browseStack = [{ label: "Objects", id: "ns=0;i=85" }];
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

$("btnConnect").onclick = async () => {
  try {
    await api("/api/config", {
      method: "PUT",
      body: JSON.stringify(gatherConfig()),
    });
    await api("/api/opcua/connect", { method: "POST" });
    await pollStatus();
    resetBrowseToRoot();
    await loadBrowse();
  } catch (e) {
    setStatus(e.message, true);
  }
};

$("btnDisconnect").onclick = async () => {
  try {
    await api("/api/opcua/disconnect", { method: "POST" });
    await pollStatus();
    resetBrowseToRoot();
    await loadBrowse();
  } catch (e) {
    setStatus(e.message, true);
  }
};

$("btnStart").onclick = async () => {
  try {
    await api("/api/config", {
      method: "PUT",
      body: JSON.stringify(gatherConfig()),
    });
    await api("/api/bridge/start", { method: "POST" });
    await pollStatus();
  } catch (e) {
    setStatus(e.message, true);
  }
};

$("btnStop").onclick = async () => {
  try {
    await api("/api/bridge/stop", { method: "POST" });
    await pollStatus();
  } catch (e) {
    setStatus(e.message, true);
  }
};

$("btnAddRow").onclick = () => addMappingRow({});

$("btnSaveConfig").onclick = () => {
  const blob = new Blob([JSON.stringify(gatherConfig(), null, 2)], {
    type: "application/json",
  });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "bridge-config.json";
  a.click();
  URL.revokeObjectURL(a.href);
};

$("fileConfig").onchange = (ev) => {
  const f = ev.target.files[0];
  if (!f) return;
  const r = new FileReader();
  r.onload = () => {
    try {
      const c = JSON.parse(r.result);
      loadFormFromConfig(c);
    } catch (e) {
      setStatus("Invalid JSON: " + e.message, true);
    }
  };
  r.readAsText(f);
};

(async function init() {
  try {
    const c = await api("/api/config");
    loadFormFromConfig(c);
  } catch {
    loadFormFromConfig({});
  }
  await pollStatus();
  setInterval(pollStatus, 1500);
  await loadBrowse();
})();
