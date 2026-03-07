import init, { check_only, compile_and_run, format_source } from "./wasm/pkg/thagore_wasm.js";
import { EXAMPLES } from "./examples.js";
import { highlightThagore, normalizeEditorText } from "./highlight.js";
import { encodeShare, decodeShare } from "./share.js";
import { Terminal } from "./terminal.js";

const editor = document.getElementById("ca");
const lineNumbers = document.getElementById("lnums");
const divider = document.getElementById("div");
const workspace = document.getElementById("ws");
const editorPanel = document.getElementById("edp");
const dropdownWrap = document.getElementById("ddwrap");
const dropdownButton = document.getElementById("btn-dd");
const dropdownMenu = document.getElementById("ddmenu");
const shareButton = document.getElementById("share-button");
const runButton = document.getElementById("btnrun");
const formatButton = document.getElementById("btnfmt");
const terminalBody = document.getElementById("tbody");
const terminalDot = document.getElementById("tdot");
const terminalState = document.getElementById("tstatxt");
const errorStat = document.getElementById("stat-err");
const elapsedStat = document.getElementById("stat-ms");
const cursorStat = document.getElementById("stat-cur");
const toast = document.getElementById("toast");
const stdlibTab = document.getElementById("stdlib-tab");

const terminal = new Terminal(terminalBody);

let wasmReady = false;
let diagnostics = [];
let debounceHandle = null;
let activeExampleId = EXAMPLES[0].id;
let dragState = null;

function showToast(text) {
  toast.textContent = text;
  toast.classList.add("visible");
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => toast.classList.remove("visible"), 2000);
}

function setTerminalStatus(state, label) {
  terminalDot.className = state ? `tdot ${state}` : "tdot";
  terminalState.textContent = label;
}

function updateErrorCount(count) {
  errorStat.textContent = `${count} error${count === 1 ? "" : "s"}`;
  errorStat.className = count > 0 ? "tbstat err" : "tbstat ok";
}

function updateElapsed(ms) {
  elapsedStat.textContent = `${ms}ms`;
}

function getSelectionOffsets(root) {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    return { start: 0, end: 0 };
  }
  const range = selection.getRangeAt(0);
  if (!root.contains(range.startContainer) || !root.contains(range.endContainer)) {
    return { start: 0, end: 0 };
  }
  const pre = range.cloneRange();
  pre.selectNodeContents(root);
  pre.setEnd(range.startContainer, range.startOffset);
  const start = pre.toString().length;
  return { start, end: start + range.toString().length };
}

function restoreSelection(root, offsets) {
  const selection = window.getSelection();
  if (!selection) {
    return;
  }

  const range = document.createRange();
  let start = offsets.start;
  let end = offsets.end;
  let foundStart = false;
  let foundEnd = false;

  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    const node = walker.currentNode;
    const len = node.textContent.length;
    if (!foundStart && start <= len) {
      range.setStart(node, start);
      foundStart = true;
    } else if (!foundStart) {
      start -= len;
    }
    if (!foundEnd && end <= len) {
      range.setEnd(node, end);
      foundEnd = true;
    } else if (!foundEnd) {
      end -= len;
    }
    if (foundStart && foundEnd) {
      break;
    }
  }

  if (!foundStart) {
    range.selectNodeContents(root);
    range.collapse(false);
  }
  if (!foundEnd) {
    range.setEnd(range.endContainer, range.endOffset);
  }

  selection.removeAllRanges();
  selection.addRange(range);
}

function getEditorContent() {
  return normalizeEditorText(editor.innerText || "");
}

function currentCursor() {
  const offsets = getSelectionOffsets(editor);
  const prefix = getEditorContent().slice(0, offsets.end);
  const lines = prefix.split("\n");
  return {
    line: Math.max(1, lines.length),
    col: (lines.at(-1) || "").length + 1,
  };
}

function updateLineNumbers(source, activeLine = currentCursor().line) {
  const lines = source.split("\n");
  lineNumbers.innerHTML = lines
    .map((_, index) => `<div class="lnum${index + 1 === activeLine ? " hl" : ""}">${index + 1}</div>`)
    .join("");
}

function updateCursorUi() {
  const position = currentCursor();
  cursorStat.textContent = `Ln ${position.line}, Col ${position.col}`;
  updateLineNumbers(getEditorContent(), position.line);
}

function renderEditor(source, currentDiagnostics = diagnostics) {
  const selection = getSelectionOffsets(editor);
  editor.innerHTML = highlightThagore(source, currentDiagnostics);
  restoreSelection(editor, selection);
  updateCursorUi();
}

function setEditorContent(source) {
  renderEditor(source, diagnostics);
}

function insertText(text) {
  document.execCommand("insertText", false, text);
}

function scheduleCheck() {
  clearTimeout(debounceHandle);
  debounceHandle = setTimeout(runCheck, 500);
}

function runCheck() {
  if (!wasmReady) {
    return;
  }
  const source = getEditorContent();
  const started = performance.now();
  try {
    diagnostics = JSON.parse(check_only(source));
  } catch {
    diagnostics = [];
  }
  renderEditor(source, diagnostics);
  updateErrorCount(diagnostics.length);
  updateElapsed(Math.round(performance.now() - started));
}

function populateExamples() {
  dropdownMenu.innerHTML = "";
  for (const example of EXAMPLES) {
    const item = document.createElement("button");
    item.type = "button";
    item.className = "ddi";
    item.innerHTML = `
      <span class="ddi-dot" style="background:${example.color}"></span>
      <span class="ddi-name">${example.name}</span>
      <span class="ddi-tag">${example.tag}</span>
    `;
    item.addEventListener("click", () => {
      activeExampleId = example.id;
      diagnostics = [];
      setEditorContent(example.code);
      updateErrorCount(0);
      markActiveExample();
      closeDropdown();
      scheduleCheck();
    });
    dropdownMenu.appendChild(item);
  }
  markActiveExample();
}

function markActiveExample() {
  const items = dropdownMenu.querySelectorAll(".ddi");
  items.forEach((item, index) => {
    item.classList.toggle("active", EXAMPLES[index].id === activeExampleId);
  });
}

function openDropdown() {
  dropdownMenu.classList.add("open");
  dropdownButton.classList.add("open");
  dropdownButton.setAttribute("aria-expanded", "true");
}

function closeDropdown() {
  dropdownMenu.classList.remove("open");
  dropdownButton.classList.remove("open");
  dropdownButton.setAttribute("aria-expanded", "false");
}

function toggleDropdown() {
  if (dropdownMenu.classList.contains("open")) {
    closeDropdown();
  } else {
    openDropdown();
  }
}

async function shareCode() {
  const link = encodeShare(getEditorContent());
  history.replaceState(null, "", link);
  await navigator.clipboard.writeText(link);
  showToast("Link copied!");
}

async function runCode() {
  if (!wasmReady) {
    return;
  }

  const source = getEditorContent();
  terminal.clear();
  terminal.writeLine("$ thagore playground run", "dim");
  setTerminalStatus("running", "running");
  runButton.classList.add("running");
  runButton.innerHTML = "<span>⏳</span> Running…";

  const started = performance.now();
  let result;
  try {
    result = compile_and_run(source);
  } catch (error) {
    terminal.writeSpacer();
    terminal.writeLine(String(error), "err");
    terminal.writeRunResult(false, Math.round(performance.now() - started));
    setTerminalStatus("err", "error");
    runButton.classList.remove("running");
    runButton.innerHTML = "<span>▶</span> Run <span class=\"btn-sub\">(Ctrl+Enter)</span>";
    return;
  }

  const elapsed = Math.round(performance.now() - started);
  updateElapsed(elapsed);
  const errors = JSON.parse(result.errors || "[]");
  diagnostics = errors;
  renderEditor(source, diagnostics);
  updateErrorCount(errors.length);

  terminal.writeSpacer();
  const output = (result.output || "").split("\n").filter((line) => line.length > 0);
  for (const line of output) {
    terminal.writeLine(line, "out");
  }
  for (const error of errors) {
    terminal.writeError(error, source);
    terminal.writeSpacer();
  }
  terminal.writeRunResult(Boolean(result.success), elapsed);
  terminal.addCursor();

  setTerminalStatus(result.success && errors.length === 0 ? "ok" : "err", result.success ? "done" : "error");
  runButton.classList.remove("running");
  runButton.innerHTML = "<span>▶</span> Run <span class=\"btn-sub\">(Ctrl+Enter)</span>";
}

function formatCode() {
  if (!wasmReady) {
    return;
  }
  const formatted = format_source(getEditorContent());
  diagnostics = [];
  setEditorContent(formatted);
  updateErrorCount(0);
  setTerminalStatus("ok", "formatted");
  scheduleCheck();
}

function handleEditorKeydown(event) {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    runCode();
    return;
  }
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
    event.preventDefault();
    formatCode();
    return;
  }
  if (event.key === "Tab") {
    event.preventDefault();
    insertText("  ");
    return;
  }
  if (event.key === "Enter") {
    const before = getEditorContent();
    const selection = getSelectionOffsets(editor);
    const prefix = before.slice(0, selection.start);
    const line = prefix.split("\n").at(-1) || "";
    const indent = (line.match(/^\s+/) || [""])[0];
    const extra = line.trimEnd().endsWith(":") ? "  " : "";
    event.preventDefault();
    insertText(`\n${indent}${extra}`);
  }
}

function bindDivider() {
  divider.addEventListener("mousedown", (event) => {
    dragState = {
      startX: event.clientX,
      startWidth: editorPanel.offsetWidth,
    };
    divider.classList.add("dragging");
    document.body.style.userSelect = "none";
    document.body.style.cursor = "col-resize";
  });

  document.addEventListener("mousemove", (event) => {
    if (!dragState || window.innerWidth <= 768) {
      return;
    }
    const nextWidth = Math.max(200, Math.min(
      dragState.startWidth + (event.clientX - dragState.startX),
      workspace.offsetWidth - 200,
    ));
    editorPanel.style.width = `${nextWidth}px`;
  });

  document.addEventListener("mouseup", () => {
    if (!dragState) {
      return;
    }
    dragState = null;
    divider.classList.remove("dragging");
    document.body.style.userSelect = "";
    document.body.style.cursor = "";
  });
}

async function boot() {
  populateExamples();
  terminal.clear();
  terminal.writeLine("thagore playground — load wasm runtime", "dim");
  terminal.addCursor();
  setTerminalStatus("", "loading");

  dropdownButton.addEventListener("click", toggleDropdown);
  document.addEventListener("click", (event) => {
    if (!dropdownWrap.contains(event.target)) {
      closeDropdown();
    }
  });
  shareButton.addEventListener("click", shareCode);
  runButton.addEventListener("click", runCode);
  formatButton.addEventListener("click", formatCode);
  stdlibTab.addEventListener("click", () => {
    showToast("Stdlib tab is a visual stub in the browser playground.");
  });

  editor.addEventListener("keydown", handleEditorKeydown);
  editor.addEventListener("input", () => {
    updateCursorUi();
    scheduleCheck();
  });
  document.addEventListener("selectionchange", () => {
    if (document.activeElement === editor) {
      updateCursorUi();
    }
  });

  bindDivider();

  try {
    await init();
    wasmReady = true;
    setTerminalStatus("ok", "ready");
  } catch (error) {
    terminal.writeSpacer();
    terminal.writeLine(String(error), "err");
    setTerminalStatus("err", "wasm error");
    return;
  }

  const shared = decodeShare();
  const initial = shared || EXAMPLES[0].code;
  setEditorContent(initial);
  updateCursorUi();
  runCheck();
}

boot();
