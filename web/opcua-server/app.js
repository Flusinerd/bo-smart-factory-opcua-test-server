const $ = (id) => document.getElementById(id);

const BROWSE_NAME_RE = /^[A-Za-z0-9_]+$/;

let selectedParent = "";

function requireBrowseName(name, label = "Browse name") {
  if (!name) throw new Error(`${label} is required`);
  if (!BROWSE_NAME_RE.test(name)) {
    throw new Error(
      `${label} may only contain letters, digits, and underscores (no spaces)`
    );
  }
}

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

async function loadConfig() {
  const c = await api("/api/config");
  $("namespaceUri").value = c.namespaceUri || "";
}

async function loadNodes() {
  const nodes = await api("/api/nodes");
  const tbody = $("nodeBody");
  tbody.innerHTML = "";
  nodes.sort((a, b) => {
    const da = a.parentBrowseName.localeCompare(b.parentBrowseName);
    return da !== 0 ? da : a.browseName.localeCompare(b.browseName);
  });
  for (const n of nodes) {
    const tr = document.createElement("tr");
    if (n.browseName === selectedParent) tr.classList.add("selected");
    const canDelete = n.role !== "simulationEnabled";
    tr.innerHTML = `
      <td><input type="radio" name="parentPick" value="${n.browseName}"
          ${n.kind !== "folder" ? "disabled" : ""}
          ${n.browseName === selectedParent && n.kind === "folder" ? "checked" : ""} /></td>
      <td>${n.kind}</td>
      <td>${n.browseName}</td>
      <td>${n.displayName}</td>
      <td>${n.parentBrowseName || "(Objects)"}</td>
      <td>${n.dataType || "—"}</td>
      <td><code>${n.nodeIdString}</code></td>
      <td>${canDelete ? `<button class="linkbtn danger" data-del="${n.browseName}">Delete</button>` : ""}</td>`;
    tbody.appendChild(tr);
    const radio = tr.querySelector('input[type="radio"]');
    if (radio && !radio.disabled) {
      radio.addEventListener("change", () => {
        selectedParent = n.browseName;
        $("folderParent").value = selectedParent;
        $("varParent").value = selectedParent;
        loadNodes();
      });
    }
    const del = tr.querySelector("[data-del]");
    if (del) {
      del.addEventListener("click", async () => {
        if (!confirm(`Delete ${n.browseName}?`)) return;
        try {
          await api(`/api/nodes/${encodeURIComponent(n.browseName)}`, { method: "DELETE" });
          setStatus(`Deleted ${n.browseName}`);
          if (selectedParent === n.browseName) selectedParent = "";
          await loadNodes();
        } catch (e) {
          setStatus(e.message, true);
        }
      });
    }
  }
}

async function loadStatus() {
  const s = await api("/api/status");
  $("simulationEnabled").checked = !!s.simulationEnabled;
  const tbody = $("sensorBody");
  tbody.innerHTML = "";
  for (const sen of s.sensors || []) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${sen.browseName}</td><td>${sen.value}</td><td>${sen.overridden}</td>`;
    tbody.appendChild(tr);
  }
}

$("btnSaveNs").addEventListener("click", async () => {
  try {
    const res = await api("/api/config", {
      method: "PUT",
      body: JSON.stringify({ namespaceUri: $("namespaceUri").value.trim() }),
    });
    setStatus(res.message || "Namespace saved");
  } catch (e) {
    setStatus(e.message, true);
  }
});

$("btnRestart").addEventListener("click", async () => {
  try {
    await api("/api/restart", { method: "POST" });
    setStatus("Server restarting…");
  } catch (e) {
    setStatus(e.message, true);
  }
});

$("simulationEnabled").addEventListener("change", async () => {
  try {
    await api("/api/simulation", {
      method: "PUT",
      body: JSON.stringify({ enabled: $("simulationEnabled").checked }),
    });
    setStatus("Simulation updated");
  } catch (e) {
    setStatus(e.message, true);
    loadStatus();
  }
});

$("btnResetOverrides").addEventListener("click", async () => {
  try {
    await api("/api/reset-overrides", { method: "POST" });
    setStatus("Overrides cleared");
    loadStatus();
  } catch (e) {
    setStatus(e.message, true);
  }
});

$("btnAddFolder").addEventListener("click", async () => {
  try {
    const browseName = $("folderBrowse").value.trim();
    requireBrowseName(browseName);
    await api("/api/nodes/folder", {
      method: "POST",
      body: JSON.stringify({
        parentBrowseName: $("folderParent").value.trim(),
        browseName,
        displayName: $("folderDisplay").value.trim(),
      }),
    });
    setStatus("Folder added");
    $("folderBrowse").value = "";
    $("folderDisplay").value = "";
    await loadNodes();
  } catch (e) {
    setStatus(e.message, true);
  }
});

$("btnAddVar").addEventListener("click", async () => {
  try {
    const browseName = $("varBrowse").value.trim();
    requireBrowseName(browseName);
    await api("/api/nodes/variable", {
      method: "POST",
      body: JSON.stringify({
        parentBrowseName: $("varParent").value.trim(),
        browseName,
        displayName: $("varDisplay").value.trim(),
        dataType: $("varType").value,
        simulation: $("varSimulation").checked,
      }),
    });
    setStatus("Variable added");
    $("varBrowse").value = "";
    $("varDisplay").value = "";
    await loadNodes();
    loadStatus();
  } catch (e) {
    setStatus(e.message, true);
  }
});

$("varType").addEventListener("change", () => {
  $("varSimulation").disabled = $("varType").value !== "Boolean";
  if ($("varSimulation").disabled) $("varSimulation").checked = false;
});

async function init() {
  try {
    await loadConfig();
    await loadNodes();
    await loadStatus();
    setInterval(loadStatus, 1000);
  } catch (e) {
    setStatus(e.message, true);
  }
}

init();
