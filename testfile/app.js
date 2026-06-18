const state = {
  activeView: "search",
  activeSettingsTab: "general",
  setup: {
    appPath: "C:\\Program Files\\fileManager",
    dbPath: "C:\\Users\\LG\\AppData\\Local\\fileManager\\db",
    smartFolders: [
      "C:\\Users\\LG\\Downloads",
      "C:\\Users\\LG\\Documents\\CS350"
    ],
    categoryFolders: [
      "C:\\Users\\LG\\Downloads",
      "C:\\Users\\LG\\Desktop"
    ],
    extensions: [".pdf", ".docx", ".txt", ".pptx", ".md", ".jpg", ".png"],
    enabledExtensions: [".pdf", ".docx", ".txt", ".pptx", ".md"]
  },
  keywordFiles: [
    {
      id: "f-1",
      name: "software_requirements_specification.pdf",
      path: "C:\\Users\\LG\\Downloads\\CS350\\software_requirements_specification.pdf",
      modified: "2026-05-30 14:34",
      keywords: ["cs350", "requirements", "team12"],
      generatedTags: ["Software Engineering", "Requirements Review", "PDF"]
    },
    {
      id: "f-2",
      name: "operating_systems_assignment.docx",
      path: "C:\\Users\\LG\\Documents\\Courses\\OS\\operating_systems_assignment.docx",
      modified: "2026-05-28 21:10",
      keywords: ["assignment", "os"],
      generatedTags: ["Operating Systems", "Assignment", "Document"]
    },
    {
      id: "f-3",
      name: "machine_learning_notes.txt",
      path: "C:\\Users\\LG\\Documents\\Research\\machine_learning_notes.txt",
      modified: "2026-05-25 09:40",
      keywords: ["ml", "notes", "paper"],
      generatedTags: ["Machine Learning", "Research Notes", "Text"]
    }
  ],
  selectedKeywordFileId: "f-1",
  searchMode: "hybrid",
  searchCache: null,
  liveSearchRequests: {
    main: 0,
    floating: 0
  },
  isIndexing: false
};

const QUICK_RESULT_LIMIT = 20;
const DATE_FILTER_LABELS = {
  any: "any",
  today: "today",
  yesterday: "yesterday",
  "this-week": "this week",
  "last-7-days": "last 7 days",
  "this-month": "this month",
  "last-30-days": "last 30 days"
};

state.modifiedDateFilter = "any";

const mockQuickResults = [
  {
    id: "q-1",
    name: "software_requirements_specification.pdf",
    path: "C:\\Users\\LG\\Downloads\\CS350\\software_requirements_specification.pdf",
    modified: "2026-05-30 14:34",
    extension: ".pdf",
    tags: ["requirements", "cs350"]
  },
  {
    id: "q-2",
    name: "software_engineering_midterm_notes.pdf",
    path: "C:\\Users\\LG\\Documents\\Courses\\CS350\\software_engineering_midterm_notes.pdf",
    modified: "2026-05-27 19:22",
    extension: ".pdf",
    tags: ["lecture", "software"]
  },
  {
    id: "q-3",
    name: "team12_project_plan.docx",
    path: "C:\\Users\\LG\\Desktop\\Team12\\team12_project_plan.docx",
    modified: "2026-05-29 10:08",
    extension: ".docx",
    tags: ["project", "team12"]
  }
];

const mockSmartResults = [
  {
    id: "s-1",
    name: "hybrid_file_search_architecture.pdf",
    path: "C:\\Users\\LG\\Downloads\\Papers\\hybrid_file_search_architecture.pdf",
    modified: "2026-05-26 16:18",
    extension: ".pdf",
    confidence: "94%",
    tags: ["semantic", "paper"]
  },
  {
    id: "s-2",
    name: "local_nlp_indexing_notes.md",
    path: "C:\\Users\\LG\\Documents\\Research\\local_nlp_indexing_notes.md",
    modified: "2026-05-25 11:42",
    extension: ".md",
    confidence: "88%",
    tags: ["nlp", "vector"]
  },
  {
    id: "s-3",
    name: "cs350_srs_reference.docx",
    path: "C:\\Users\\LG\\Documents\\Courses\\CS350\\cs350_srs_reference.docx",
    modified: "2026-05-24 08:52",
    extension: ".docx",
    confidence: "81%",
    tags: ["requirements", "reference"]
  }
];

function shouldUseHttpApi() {
  return window.FILE_MANAGER_USE_HTTP_API === true ||
    (!window.FileManagerBackend && window.location.protocol.startsWith("http"));
}

const BackendApi = {
  async getConfig() {
    if (window.FileManagerBackend?.getConfig) {
      return window.FileManagerBackend.getConfig();
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/config", null, "GET");
    }

    return {
      isConfigured: false,
      appPath: state.setup.appPath,
      dbPath: state.setup.dbPath,
      smartFolders: state.setup.smartFolders,
      categoryFolders: state.setup.categoryFolders,
      extensions: state.setup.enabledExtensions,
      hotkey: "Alt + Space"
    };
  },

  async getStatus() {
    if (window.FileManagerBackend?.getStatus) {
      return window.FileManagerBackend.getStatus();
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/status", null, "GET");
    }

    return {
      isConfigured: true,
      isIndexing: false,
      indexedCount: 42180,
      filesSeen: 42180,
      filesIndexed: 42180,
      exceptions: 18,
      message: "Mock data"
    };
  },

  async getFiles(query = "") {
    if (window.FileManagerBackend?.getFiles) {
      return window.FileManagerBackend.getFiles(query);
    }

    if (shouldUseHttpApi()) {
      const params = new URLSearchParams();
      if (query) {
        params.set("query", query);
      }
      params.set("limit", "250");
      return requestJson(`/api/files?${params.toString()}`, null, "GET");
    }

    return state.keywordFiles;
  },

  async search(query, mode = state.searchMode, modifiedDate = state.modifiedDateFilter) {
    const normalizedMode = normalizeSearchMode(mode);
    const normalizedModifiedDate = normalizeDateFilter(modifiedDate);

    if (window.FileManagerBackend?.search) {
      return window.FileManagerBackend.search(query, normalizedMode, normalizedModifiedDate);
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/search", { query, mode: normalizedMode, modifiedDate: normalizedModifiedDate });
    }

    const normalizedQuery = query.trim().toLowerCase();
    const quickResults = normalizedMode === "smart"
      ? []
      : mockQuickResults.filter((file) => matchesSearch(file, normalizedQuery));
    const smartResults = normalizedMode === "quick" || !normalizedQuery
      ? []
      : mockSmartResults.filter((file) => matchesSemantic(file, normalizedQuery));

    return {
      quickResults,
      smartResults,
      parsedQuery: inferQueryDetails(normalizedQuery, normalizedMode, normalizedModifiedDate),
      timings: {
        quick: normalizedMode === "smart" ? "off" : "0.08s",
        smart: normalizedMode === "quick" ? "off" : smartResults.length ? "0.74s" : "ready"
      }
    };
  },

  async saveSetup(settings) {
    if (window.FileManagerBackend?.saveSetup) {
      return window.FileManagerBackend.saveSetup(settings);
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/setup", settings);
    }

    return { ok: true };
  },

  async saveSettings(settings) {
    if (window.FileManagerBackend?.saveSettings) {
      return window.FileManagerBackend.saveSettings(settings);
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/settings", settings);
    }

    return { ok: true };
  },

  async saveKeywords(fileId, keywords) {
    if (window.FileManagerBackend?.saveKeywords) {
      return window.FileManagerBackend.saveKeywords(fileId, keywords);
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/keywords", { fileId, keywords });
    }

    return { ok: true };
  },

  async openFile(path) {
    if (window.FileManagerBackend?.openFile) {
      return window.FileManagerBackend.openFile(path);
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/files/open", { path });
    }

    showToast(`Open request: ${path}`);
    return { ok: true };
  },

  async rebuildIndex() {
    if (window.FileManagerBackend?.rebuildIndex) {
      return window.FileManagerBackend.rebuildIndex();
    }

    if (shouldUseHttpApi()) {
      return requestJson("/api/index/rebuild", {});
    }

    return { ok: true, message: "Mock index rebuild started" };
  },

  async chooseFolder(initialPath = "", title = "Select folder") {
    if (window.FileManagerBackend?.chooseFolder) {
      return window.FileManagerBackend.chooseFolder(initialPath, title);
    }

    if (shouldUseHttpApi()) {
      try {
        return await requestJson("/api/dialog/folder", { initialPath, title });
      } catch {
        return { ok: false, path: "" };
      }
    }

    const path = window.prompt(title, initialPath);
    return { ok: Boolean(path), path };
  }
};

async function requestJson(url, body, method = "POST") {
  const options = { method };
  if (method !== "GET") {
    options.headers = { "Content-Type": "application/json" };
    options.body = JSON.stringify(body ?? {});
  }

  const response = await fetch(url, options);

  if (!response.ok) {
    const message = await response.text();
    throw new Error(message || `Request failed: ${response.status}`);
  }

  return response.json();
}

function matchesSearch(file, normalizedQuery) {
  if (!normalizedQuery) {
    return false;
  }

  const queryTerms = splitQuickTerms(normalizedQuery);
  const fileTerms = splitQuickTerms(`${file.name} ${file.path} ${file.tags.join(" ")}`);
  return queryTerms.every((queryTerm) => fileTerms.some((fileTerm) => fileTerm.startsWith(queryTerm)));
}

function splitQuickTerms(value) {
  return value
    .toLowerCase()
    .split(/[\\/\s\-_]+/)
    .map((term) => term.trim())
    .filter(Boolean);
}

function matchesSemantic(file, normalizedQuery) {
  if (!normalizedQuery) {
    return false;
  }

  const semanticTerms = ["paper", "software", "engineering", "week", "requirements", "nlp", "search"];
  const searchable = `${file.name} ${file.path} ${file.extension} ${file.tags.join(" ")}`.toLowerCase();
  return semanticTerms.some((term) => normalizedQuery.includes(term)) || searchable.includes(normalizedQuery);
}

function normalizeSearchMode(mode) {
  return mode === "quick" || mode === "smart" ? mode : "hybrid";
}

function normalizeDateFilter(value) {
  return Object.prototype.hasOwnProperty.call(DATE_FILTER_LABELS, value) ? value : "any";
}

function searchModeLabel(mode) {
  if (mode === "quick") {
    return "quick only";
  }

  if (mode === "smart") {
    return "smart only";
  }

  return "hybrid";
}

function getSearchMode(target = "main") {
  const groupName = target === "floating" ? "floatingSearchMode" : "searchMode";
  return normalizeSearchMode(document.querySelector(`input[name="${groupName}"]:checked`)?.value || state.searchMode);
}

function setSearchMode(mode) {
  const normalizedMode = normalizeSearchMode(mode);
  state.searchMode = normalizedMode;

  document.querySelectorAll('input[name="searchMode"], input[name="floatingSearchMode"]').forEach((input) => {
    input.checked = input.value === normalizedMode;
  });
}

function getDateFilter(target = "main") {
  const selectId = target === "floating" ? "floatingModifiedDateFilter" : "modifiedDateFilter";
  return normalizeDateFilter(document.getElementById(selectId)?.value || state.modifiedDateFilter);
}

function setDateFilter(value) {
  const normalizedValue = normalizeDateFilter(value);
  state.modifiedDateFilter = normalizedValue;

  ["modifiedDateFilter", "floatingModifiedDateFilter"].forEach((id) => {
    const select = document.getElementById(id);
    if (select) {
      select.value = normalizedValue;
    }
  });
}

function inferQueryDetails(normalizedQuery, mode = state.searchMode, modifiedDate = state.modifiedDateFilter) {
  const details = [];

  if (normalizedQuery.includes("pdf") || normalizedQuery.includes("paper") || normalizedQuery.includes("thesis")) {
    details.push("Type: PDF");
  } else if (normalizedQuery.includes("photo") || normalizedQuery.includes("image")) {
    details.push("Type: image");
  } else {
    details.push("Type: any");
  }

  details.push(`Modified: ${DATE_FILTER_LABELS[normalizeDateFilter(modifiedDate)]}`);

  details.push(`Mode: ${searchModeLabel(normalizeSearchMode(mode))}`);
  return details;
}

function el(tagName, className, text) {
  const node = document.createElement(tagName);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function clearNode(node) {
  while (node.firstChild) {
    node.removeChild(node.firstChild);
  }
}

function setView(viewName) {
  state.activeView = viewName;

  document.querySelectorAll(".nav-item").forEach((button) => {
    button.classList.toggle("is-active", button.dataset.view === viewName);
  });

  document.querySelectorAll("[data-view-panel]").forEach((panel) => {
    panel.classList.toggle("is-active", panel.dataset.viewPanel === viewName);
  });

  const titles = {
    search: ["Overlay UI", "Search files"],
    setup: ["Initial Setup", "Configure fileManager"],
    settings: ["Settings", "Manage preferences"],
    keywords: ["Custom Keywords", "Improve categorization"]
  };

  document.getElementById("viewEyebrow").textContent = titles[viewName][0];
  document.getElementById("viewTitle").textContent = titles[viewName][1];
}

function renderResultList(container, results, options = {}) {
  clearNode(container);

  if (!results.length) {
    container.appendChild(el("div", "empty-state", "No results found"));
    return;
  }

  results.forEach((file) => {
    const item = el("article", "result-item");
    const titleLine = el("div", "result-title-line");
    titleLine.appendChild(el("strong", "", file.name));

    if (file.confidence) {
      titleLine.appendChild(el("span", "tag", file.confidence));
    } else {
      titleLine.appendChild(el("span", "tag", file.extension));
    }

    item.appendChild(titleLine);

    if (!options.compact) {
      item.appendChild(el("p", "result-path", file.path));
      item.appendChild(el("p", "meta-line", `Modified ${file.modified}`));
    }

    const actions = el("div", "result-actions");
    const openButton = el("button", "small-button", "Open");
    openButton.type = "button";
    openButton.addEventListener("click", () => BackendApi.openFile(file.path));

    const revealButton = el("button", "small-button", "Reveal");
    revealButton.type = "button";
    revealButton.addEventListener("click", () => showToast(`Reveal request: ${file.path}`));

    actions.append(openButton, revealButton);
    item.appendChild(actions);

    container.appendChild(item);
  });
}

function limitQuickResults(results) {
  return (results || []).slice(0, QUICK_RESULT_LIMIT);
}

function renderQueryDetails(details) {
  const container = document.getElementById("queryDetails");
  clearNode(container);

  details.forEach((detail) => {
    container.appendChild(el("span", "", detail));
  });
}

async function runSearch(query, target = "main") {
  const grid = target === "main" ? document.getElementById("resultGrid") : document.getElementById("floatingResultGrid");
  const quickContainer = target === "main" ? document.getElementById("quickResults") : document.getElementById("floatingQuickResults");
  const smartContainer = target === "main" ? document.getElementById("smartResults") : document.getElementById("floatingSmartResults");
  const smartMetric = target === "main" ? document.getElementById("smartMetric") : document.getElementById("floatingSmartMetric");
  const quickMetric = target === "main" ? document.getElementById("quickMetric") : document.getElementById("floatingQuickMetric");
  const mode = getSearchMode(target);
  const modifiedDate = getDateFilter(target);
  const wantsSmart = mode !== "quick";
  state.liveSearchRequests[target] += 1;
  setSearchMode(mode);
  setDateFilter(modifiedDate);

  if (state.isIndexing) {
    grid.classList.remove("smart-pending", "no-quick");
    renderResultList(quickContainer, []);
    renderResultList(smartContainer, []);
    smartMetric.textContent = "blocked";
    if (quickMetric) {
      quickMetric.textContent = "blocked";
    }
    showToast("Search is disabled while indexing.");
    return;
  }

  grid.classList.remove("no-quick");

  if (wantsSmart) {
    grid.classList.add("smart-pending");
    smartMetric.textContent = "processing";
    clearNode(smartContainer);
    smartContainer.appendChild(el("div", "empty-state", "Processing"));
  } else {
    grid.classList.remove("smart-pending");
    smartMetric.textContent = "off";
    renderResultList(smartContainer, []);
  }

  try {
    const results = await BackendApi.search(query, mode, modifiedDate);
    state.searchCache = results;
    if (results.message) {
      showToast(results.message);
    }

    const quickResults = limitQuickResults(results.quickResults);
    renderResultList(quickContainer, quickResults);
    if (target === "main") {
      renderQueryDetails(results.parsedQuery || inferQueryDetails(query.toLowerCase(), mode, modifiedDate));
    }
    if (quickMetric) {
      quickMetric.textContent = results.timings?.quick || "0.10s";
    }

    if (!wantsSmart) {
      grid.classList.remove("smart-pending", "no-quick");
      smartMetric.textContent = results.timings?.smart || "off";
      renderResultList(smartContainer, []);
      return;
    }

    window.setTimeout(() => {
      const smartResults = results.smartResults || [];
      grid.classList.remove("smart-pending", "no-quick");
      if (!quickResults.length && smartResults.length) {
        grid.classList.add("no-quick");
      }

      smartMetric.textContent = results.timings?.smart || "ready";
      renderResultList(smartContainer, smartResults, { compact: true });
    }, shouldUseHttpApi() ? 0 : 420);
  } catch (error) {
    grid.classList.remove("smart-pending", "no-quick");
    renderResultList(quickContainer, []);
    renderResultList(smartContainer, []);
    showToast(error.message);
  }
}

async function runLiveQuickSearch(query, target = "main") {
  const mode = getSearchMode(target);
  const modifiedDate = getDateFilter(target);
  const requestId = state.liveSearchRequests[target] + 1;
  state.liveSearchRequests[target] = requestId;
  setDateFilter(modifiedDate);

  const grid = target === "main" ? document.getElementById("resultGrid") : document.getElementById("floatingResultGrid");
  const quickContainer = target === "main" ? document.getElementById("quickResults") : document.getElementById("floatingQuickResults");
  const smartContainer = target === "main" ? document.getElementById("smartResults") : document.getElementById("floatingSmartResults");
  const quickMetric = target === "main" ? document.getElementById("quickMetric") : document.getElementById("floatingQuickMetric");
  const smartMetric = target === "main" ? document.getElementById("smartMetric") : document.getElementById("floatingSmartMetric");

  if (state.isIndexing) {
    renderResultList(quickContainer, []);
    if (quickMetric) {
      quickMetric.textContent = "blocked";
    }
    return;
  }

  try {
    const results = await BackendApi.search(query, "quick", modifiedDate);
    if (state.liveSearchRequests[target] !== requestId) {
      return;
    }

    grid.classList.remove("smart-pending", "no-quick");
    renderResultList(quickContainer, limitQuickResults(results.quickResults));
    if (quickMetric) {
      quickMetric.textContent = results.timings?.quick || "ready";
    }

    if (target === "main") {
      renderQueryDetails(inferQueryDetails(query.toLowerCase(), mode, modifiedDate));
    }

    if (mode === "quick") {
      smartMetric.textContent = "off";
      renderResultList(smartContainer, []);
    } else {
      smartMetric.textContent = "press Search";
      clearNode(smartContainer);
      smartContainer.appendChild(el("div", "empty-state", "Press Search to refresh smart results"));
    }
  } catch (error) {
    if (state.liveSearchRequests[target] === requestId) {
      showToast(error.message);
    }
  }
}

function renderFolders(containerId, folders, removable = true) {
  const container = document.getElementById(containerId);
  clearNode(container);

  folders.forEach((folder, index) => {
    const row = el("div", "folder-row");
    row.appendChild(el("span", "", folder));

    if (removable) {
      const button = el("button", "remove-button", "x");
      button.type = "button";
      button.setAttribute("aria-label", `Remove ${folder}`);
      button.addEventListener("click", () => {
        folders.splice(index, 1);
        renderAllConfiguration();
      });
      row.appendChild(button);
    }

    container.appendChild(row);
  });
}

function renderExtensions(containerId) {
  const container = document.getElementById(containerId);
  clearNode(container);

  state.setup.extensions.forEach((extension) => {
    const label = el("label", "extension-chip");
    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.value = extension;
    checkbox.checked = state.setup.enabledExtensions.includes(extension);
    checkbox.addEventListener("change", () => {
      if (checkbox.checked) {
        state.setup.enabledExtensions = Array.from(new Set([...state.setup.enabledExtensions, extension]));
      } else {
        state.setup.enabledExtensions = state.setup.enabledExtensions.filter((item) => item !== extension);
      }
    });

    label.append(checkbox, document.createTextNode(extension));
    container.appendChild(label);
  });
}

function renderAllConfiguration() {
  renderFolders("setupSmartFolders", state.setup.smartFolders);
  renderFolders("setupCategoryFolders", state.setup.categoryFolders);
  renderFolders("settingsSmartFolders", state.setup.smartFolders);
  renderExtensions("setupExtensions");
  renderExtensions("settingsExtensions");
}

function syncSetupFromInputs() {
  state.setup.appPath = document.getElementById("setupAppPath").value.trim();
  state.setup.dbPath = document.getElementById("setupDbPath").value.trim();
  document.getElementById("settingsAppPath").value = state.setup.appPath;
  document.getElementById("settingsDbPath").value = state.setup.dbPath;
}

function syncSettingsFromInputs() {
  state.setup.appPath = document.getElementById("settingsAppPath").value.trim();
  state.setup.dbPath = document.getElementById("settingsDbPath").value.trim();
  document.getElementById("setupAppPath").value = state.setup.appPath;
  document.getElementById("setupDbPath").value = state.setup.dbPath;
}

function applyConfig(config) {
  if (!config) {
    return;
  }

  const extensions = config.extensions || state.setup.enabledExtensions;
  state.setup.appPath = config.appPath || state.setup.appPath;
  state.setup.dbPath = config.dbPath || state.setup.dbPath;
  state.setup.smartFolders = config.smartFolders || state.setup.smartFolders;
  state.setup.categoryFolders = config.categoryFolders || state.setup.categoryFolders;
  state.setup.enabledExtensions = extensions;
  state.setup.extensions = Array.from(new Set([...state.setup.extensions, ...extensions]));

  document.getElementById("setupAppPath").value = state.setup.appPath;
  document.getElementById("setupDbPath").value = state.setup.dbPath;
  document.getElementById("settingsAppPath").value = state.setup.appPath;
  document.getElementById("settingsDbPath").value = state.setup.dbPath;
  document.getElementById("hotkeyInput").value = config.hotkey || "Alt + Space";
  renderAllConfiguration();
}

function formatNumber(value) {
  return Number(value || 0).toLocaleString();
}

function updateStatus(status) {
  if (!status) {
    return;
  }

  const indexedCount = status.indexedCount || 0;
  const filesSeen = status.filesSeen || 0;
  const filesIndexed = status.filesIndexed || 0;
  const exceptions = status.exceptions || 0;
  const progressPercent = filesSeen > 0 ? Math.min(100, Math.round((filesIndexed / filesSeen) * 100)) : 0;
  const stateText = status.isIndexing ? "indexing" : status.isConfigured ? "ready" : "setup";
  state.isIndexing = Boolean(status.isIndexing);

  const statusDot = document.querySelector(".status-dot");
  statusDot?.classList.toggle("is-indexing", state.isIndexing);
  statusDot?.classList.toggle("is-setup-required", !status.isConfigured && !state.isIndexing);
  document.getElementById("sidebarStatusText").textContent = status.isIndexing ? "Indexing" : status.isConfigured ? "Index ready" : "Setup required";
  document.getElementById("sidebarStatusSubtext").textContent = `${formatNumber(indexedCount)} files`;
  document.getElementById("indexedCountText").textContent = formatNumber(indexedCount);
  document.getElementById("filesSeenText").textContent = formatNumber(filesSeen);
  document.getElementById("filesIndexedText").textContent = formatNumber(filesIndexed);
  document.getElementById("exceptionsText").textContent = formatNumber(exceptions);
  document.getElementById("indexStatusMessage").textContent = status.message || "Idle";
  document.getElementById("indexStatusState").textContent = stateText;
  document.getElementById("indexProgressFill").style.width = `${progressPercent}%`;
  document.getElementById("filesIndexedFill").style.width = `${progressPercent}%`;
  setSearchDisabled(state.isIndexing);
}

function setSearchDisabled(isDisabled) {
  const disabledElements = [
    "searchInput",
    "searchButton",
    "floatingSearchInput",
    "floatingSearchButton",
    "openOverlayButton"
  ];

  disabledElements.forEach((id) => {
    const element = document.getElementById(id);
    if (!element) {
      return;
    }

    element.disabled = isDisabled;
    element.setAttribute("aria-disabled", String(isDisabled));
  });

  document.querySelectorAll(".search-bar").forEach((bar) => {
    bar.classList.toggle("is-disabled", isDisabled);
  });

  document.querySelectorAll('input[name="searchMode"], input[name="floatingSearchMode"]').forEach((input) => {
    input.disabled = isDisabled;
  });

  document.querySelectorAll("#modifiedDateFilter, #floatingModifiedDateFilter").forEach((select) => {
    select.disabled = isDisabled;
  });
}

async function refreshStatus() {
  try {
    updateStatus(await BackendApi.getStatus());
  } catch (error) {
    showToast(error.message);
  }
}

async function refreshKeywordFiles(filter = "") {
  try {
    const files = await BackendApi.getFiles(filter);
    if (Array.isArray(files) && files.length > 0) {
      state.keywordFiles = files.map((file) => ({
        id: file.id || file.path,
        name: file.name,
        path: file.path,
        modified: file.modified,
        keywords: file.keywords || [],
        generatedTags: file.generatedTags || []
      }));
      if (!state.keywordFiles.some((file) => file.id === state.selectedKeywordFileId)) {
        state.selectedKeywordFileId = state.keywordFiles[0].id;
      }
    } else if (shouldUseHttpApi()) {
      state.keywordFiles = [];
      state.selectedKeywordFileId = "";
    }

    renderKeywordFiles(filter);
    renderKeywordEditor();
  } catch (error) {
    showToast(error.message);
  }
}

function renderKeywordFiles(filter = "") {
  const container = document.getElementById("keywordFileList");
  clearNode(container);

  const normalizedFilter = filter.toLowerCase();
  const files = state.keywordFiles.filter((file) => {
    return `${file.name} ${file.path} ${(file.keywords || []).join(" ")} ${(file.generatedTags || []).join(" ")}`
      .toLowerCase()
      .includes(normalizedFilter);
  });

  files.forEach((file) => {
    const row = el("button", "file-row");
    row.type = "button";
    row.classList.toggle("is-selected", file.id === state.selectedKeywordFileId);
    row.appendChild(el("strong", "", file.name));
    row.appendChild(el("span", "meta-line", file.modified));
    row.addEventListener("click", () => {
      state.selectedKeywordFileId = file.id;
      renderKeywordFiles(document.getElementById("keywordFilter").value);
      renderKeywordEditor();
    });
    container.appendChild(row);
  });

  if (!files.length) {
    container.appendChild(el("div", "empty-state", "No results found"));
  }
}

function renderKeywordEditor() {
  const file = state.keywordFiles.find((item) => item.id === state.selectedKeywordFileId);
  const name = document.getElementById("selectedFileName");
  const path = document.getElementById("selectedFilePath");
  const cloud = document.getElementById("keywordCloud");
  const generatedCloud = document.getElementById("generatedTagCloud");

  clearNode(cloud);
  clearNode(generatedCloud);

  if (!file) {
    name.textContent = "No file selected";
    path.textContent = "";
    return;
  }

  name.textContent = file.name;
  path.textContent = file.path;

  (file.generatedTags || []).forEach((tag) => {
    generatedCloud.appendChild(el("span", "generated-tag-chip", tag));
  });

  if (!(file.generatedTags || []).length) {
    generatedCloud.appendChild(el("span", "empty-state compact", "No generated tags yet"));
  }

  (file.keywords || []).forEach((keyword) => {
    const chip = el("span", "keyword-chip");
    chip.appendChild(document.createTextNode(keyword));

    const removeButton = el("button", "", "x");
    removeButton.type = "button";
    removeButton.setAttribute("aria-label", `Remove ${keyword}`);
    removeButton.addEventListener("click", () => {
      file.keywords = file.keywords.filter((item) => item !== keyword);
      renderKeywordEditor();
      renderKeywordFiles(document.getElementById("keywordFilter").value);
    });

    chip.appendChild(removeButton);
    cloud.appendChild(chip);
  });
}

function setSettingsTab(tabName) {
  state.activeSettingsTab = tabName;

  document.querySelectorAll("[data-tab]").forEach((tab) => {
    tab.classList.toggle("is-active", tab.dataset.tab === tabName);
  });

  document.querySelectorAll("[data-tab-panel]").forEach((panel) => {
    panel.classList.toggle("is-active", panel.dataset.tabPanel === tabName);
  });
}

function showToast(message) {
  const toast = document.getElementById("toast");
  toast.textContent = message;
  toast.classList.add("is-visible");
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => {
    toast.classList.remove("is-visible");
  }, 2400);
}

function postNativeMessage(message) {
  if (!window.chrome?.webview?.postMessage) {
    return false;
  }

  window.chrome.webview.postMessage(message);
  return true;
}

function exitNativeOverlayMode() {
  document.body.classList.remove("native-overlay-mode");
}

function openFloatingOverlay(options = {}) {
  if (state.isIndexing) {
    showToast("Search is disabled while indexing.");
    return;
  }

  if (options.nativeOverlay) {
    document.body.classList.add("native-overlay-mode");
  } else {
    exitNativeOverlayMode();
  }

  const backdrop = document.getElementById("overlayBackdrop");
  const mainInput = document.getElementById("searchInput");
  const floatingInput = document.getElementById("floatingSearchInput");
  setDateFilter(getDateFilter("main"));

  floatingInput.value = mainInput.value;
  backdrop.classList.add("is-open");
  backdrop.setAttribute("aria-hidden", "false");
  floatingInput.focus();
  floatingInput.select();
  runSearch(floatingInput.value, "floating");
}

function closeFloatingOverlay() {
  const backdrop = document.getElementById("overlayBackdrop");
  const wasNativeOverlay = document.body.classList.contains("native-overlay-mode");
  backdrop.classList.remove("is-open");
  backdrop.setAttribute("aria-hidden", "true");
  exitNativeOverlayMode();

  if (wasNativeOverlay) {
    postNativeMessage({ type: "nativeOverlayClosed" });
  }
}

function attachEvents() {
  document.querySelectorAll("[data-view]").forEach((button) => {
    button.addEventListener("click", () => setView(button.dataset.view));
  });

  document.querySelectorAll("[data-tab]").forEach((button) => {
    button.addEventListener("click", () => setSettingsTab(button.dataset.tab));
  });

  document.getElementById("searchButton").addEventListener("click", () => {
    runSearch(document.getElementById("searchInput").value, "main");
  });

  document.getElementById("searchInput").addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      runSearch(event.currentTarget.value, "main");
    }
  });

  document.getElementById("searchInput").addEventListener("input", (event) => {
    runLiveQuickSearch(event.currentTarget.value, "main");
  });

  document.getElementById("floatingSearchButton").addEventListener("click", () => {
    runSearch(document.getElementById("floatingSearchInput").value, "floating");
  });

  document.getElementById("floatingSearchInput").addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      runSearch(event.currentTarget.value, "floating");
    }
  });

  document.getElementById("floatingSearchInput").addEventListener("input", (event) => {
    runLiveQuickSearch(event.currentTarget.value, "floating");
  });

  document.querySelectorAll("#modifiedDateFilter, #floatingModifiedDateFilter").forEach((select) => {
    select.addEventListener("change", () => {
      setDateFilter(select.value);
      if (state.isIndexing) {
        return;
      }

      const target = select.id === "floatingModifiedDateFilter" ? "floating" : "main";
      const searchInput = target === "floating"
        ? document.getElementById("floatingSearchInput")
        : document.getElementById("searchInput");
      runSearch(searchInput.value, target);
    });
  });

  document.querySelectorAll('input[name="searchMode"], input[name="floatingSearchMode"]').forEach((input) => {
    input.addEventListener("change", () => {
      if (!input.checked) {
        return;
      }

      setSearchMode(input.value);
      if (state.isIndexing) {
        return;
      }

      const target = input.name === "floatingSearchMode" ? "floating" : "main";
      const searchInput = target === "floating"
        ? document.getElementById("floatingSearchInput")
        : document.getElementById("searchInput");
      runSearch(searchInput.value, target);
    });
  });

  document.getElementById("openOverlayButton").addEventListener("click", openFloatingOverlay);

  document.getElementById("overlayBackdrop").addEventListener("click", (event) => {
    if (event.target.id === "overlayBackdrop") {
      closeFloatingOverlay();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      closeFloatingOverlay();
    }

    if (event.altKey && event.code === "Space") {
      event.preventDefault();
      openFloatingOverlay();
    }
  });

  document.querySelectorAll("[data-add-folder]").forEach((button) => {
    button.addEventListener("click", async () => {
      const isSmartFolder = button.dataset.addFolder === "smart";
      const existingFolders = isSmartFolder ? state.setup.smartFolders : state.setup.categoryFolders;
      const selected = await BackendApi.chooseFolder(
        existingFolders[0] || state.setup.appPath,
        isSmartFolder ? "Select Smart Search folder" : "Select Categorization folder"
      );
      const folderPath = selected?.path;
      if (!selected?.ok || !folderPath) {
        return;
      }

      if (isSmartFolder) {
        if (!state.setup.smartFolders.some((folder) => folder.toLowerCase() === folderPath.toLowerCase())) {
          state.setup.smartFolders.push(folderPath);
        }
      } else {
        if (!state.setup.categoryFolders.some((folder) => folder.toLowerCase() === folderPath.toLowerCase())) {
          state.setup.categoryFolders.push(folderPath);
        }
      }
      renderAllConfiguration();
    });
  });

  document.getElementById("resetSetupButton").addEventListener("click", () => {
    document.getElementById("setupAppPath").value = "C:\\Program Files\\fileManager";
    document.getElementById("setupDbPath").value = "C:\\Users\\LG\\AppData\\Local\\fileManager\\db";
    showToast("Setup fields reset");
  });

  document.getElementById("saveSetupButton").addEventListener("click", async () => {
    try {
      syncSetupFromInputs();
      const response = await BackendApi.saveSetup(state.setup);
      showToast(response.message || "Setup saved");
      await refreshStatus();
      await refreshKeywordFiles(document.getElementById("keywordFilter").value);
    } catch (error) {
      showToast(error.message);
    }
  });

  document.getElementById("saveGeneralButton").addEventListener("click", async () => {
    try {
      syncSettingsFromInputs();
      const hotkey = document.getElementById("hotkeyInput").value.trim();
      const response = await BackendApi.saveSettings({ ...state.setup, hotkey });
      showToast(response.message || "Settings saved");
      await refreshStatus();
    } catch (error) {
      showToast(error.message);
    }
  });

  document.getElementById("saveScopeButton").addEventListener("click", async () => {
    try {
      const response = await BackendApi.saveSettings({ smartFolders: state.setup.smartFolders, enabledExtensions: state.setup.enabledExtensions });
      showToast(response.message || "Scope applied");
      await refreshStatus();
    } catch (error) {
      showToast(error.message);
    }
  });

  document.getElementById("refreshStatusButton").addEventListener("click", refreshStatus);

  document.getElementById("rebuildIndexButton").addEventListener("click", async () => {
    try {
      const response = await BackendApi.rebuildIndex();
      showToast(response.message || "Index rebuild started");
      await refreshStatus();
    } catch (error) {
      showToast(error.message);
    }
  });

  document.getElementById("keywordFilter").addEventListener("input", (event) => {
    renderKeywordFiles(event.currentTarget.value);
  });

  document.getElementById("keywordForm").addEventListener("submit", (event) => {
    event.preventDefault();
    const input = document.getElementById("keywordInput");
    const keyword = input.value.trim();
    const file = state.keywordFiles.find((item) => item.id === state.selectedKeywordFileId);

    if (!keyword || !file) {
      return;
    }

    file.keywords = Array.from(new Set([...file.keywords, keyword]));
    input.value = "";
    renderKeywordEditor();
    renderKeywordFiles(document.getElementById("keywordFilter").value);
  });

  document.getElementById("saveKeywordButton").addEventListener("click", async () => {
    try {
      const file = state.keywordFiles.find((item) => item.id === state.selectedKeywordFileId);
      if (!file) {
        return;
      }

      const response = await BackendApi.saveKeywords(file.id, file.keywords);
      showToast(response.message || "Keywords saved");
      await refreshKeywordFiles(document.getElementById("keywordFilter").value);
    } catch (error) {
      showToast(error.message);
    }
  });
}

async function loadBackendState() {
  try {
    applyConfig(await BackendApi.getConfig());
    await refreshStatus();
    await refreshKeywordFiles();
  } catch (error) {
    showToast(error.message);
  }
}

async function init() {
  attachEvents();
  renderAllConfiguration();
  renderKeywordFiles();
  renderKeywordEditor();
  await loadBackendState();
  runSearch(document.getElementById("searchInput").value, "main");
  window.setInterval(refreshStatus, 2500);
}

document.addEventListener("DOMContentLoaded", init);
