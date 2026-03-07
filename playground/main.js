import init, { check_only, compile_and_run, format_source } from "./wasm/pkg/thagore_wasm.js";
import { EXAMPLES } from "./examples.js";
import { highlightThagore, normalizeEditorText } from "./highlight.js";
import { encodeShare, decodeShare } from "./share.js";
import { Terminal } from "./terminal.js";

const editor = document.getElementById("editor");
const examples = document.getElementById("examples");
const runButton = document.getElementById("run-button");
const formatButton = document.getElementById("format-button");
const shareButton = document.getElementById("share-button");
const terminalEl = document.getElementById("terminal");
const statusText = document.getElementById("status-text");
const errorCountEl = document.getElementById("error-count");
const elapsedEl = document.getElementById("elapsed");
const toastEl = document.getElementById("toast");

const terminal = new Terminal(terminalEl);

let wasmReady = false;
let diagnostics = [];
let debounceHandle = null;

function showStatus(text) {
  statusText.textContent = text;
}

function updateErrorCount(count) {
  errorCountEl.textContent = `${count} error${count === 1 ? "" : "s"}`;
}

function updateElapsed(ms) {
  elapsedEl.textContent = `${ms}ms`;
}

function showToast(text) {
  toastEl.textContent = text;
  toastEl.classList.add("visible");
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => toastEl.classList.remove("visible"), 2000);
}

function getSelectionOffsets(root) {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    return { start: 0, end: 0 };
  }
  const range = selection.getRangeAt(0);
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

function renderEditor(source, currentDiagnostics = diagnostics) {
  const selection = getSelectionOffsets(editor);
  editor.innerHTML = highlightThagore(source, currentDiagnostics);
  restoreSelection(editor, selection);
}

function setEditorContent(source) {
  renderEditor(source, diagnostics);
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

function insertText(text) {
  document.execCommand("insertText", false, text);
}

async function runCode() {
  if (!wasmReady) {
    return;
  }
  const source = getEditorContent();
  terminal.clear();
  terminal.writeLine("Running...", "system");
  const started = performance.now();
  let result = null;
  try {
    result = compile_and_run(source);
  } catch (error) {
    terminal.writeLine(String(error), "error");
    return;
  }
  const elapsed = Math.round(performance.now() - started);
  updateElapsed(elapsed);
  const errors = JSON.parse(result.errors || "[]");
  diagnostics = errors;
  renderEditor(source, diagnostics);
  updateErrorCount(errors.length);
  for (const line of (result.output || "").split("\n")) {
    if (line.length > 0) {
      terminal.writeLine(line, "output");
    }
  }
  for (const error of errors) {
    terminal.writeError(error);
  }
  terminal.writeRunResult(result, elapsed);
}

function formatCode() {
  if (!wasmReady) {
    return;
  }
  const formatted = format_source(getEditorContent());
  diagnostics = [];
  setEditorContent(formatted);
  updateErrorCount(0);
  showStatus("Formatted");
  scheduleCheck();
}

async function shareCode() {
  const link = encodeShare(getEditorContent());
  history.replaceState(null, "", link);
  await navigator.clipboard.writeText(link);
  showToast("Link copied!");
}

function populateExamples() {
  for (const example of EXAMPLES) {
    const option = document.createElement("option");
    option.value = example.name;
    option.textContent = example.name;
    examples.appendChild(option);
  }
  examples.addEventListener("change", () => {
    const example = EXAMPLES.find((item) => item.name === examples.value);
    if (example) {
      diagnostics = [];
      setEditorContent(example.code);
      updateErrorCount(0);
      scheduleCheck();
    }
  });
}

function handleKeydown(event) {
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

async function boot() {
  populateExamples();
  editor.addEventListener("keydown", handleKeydown);
  editor.addEventListener("input", () => scheduleCheck());
  runButton.addEventListener("click", runCode);
  formatButton.addEventListener("click", formatCode);
  shareButton.addEventListener("click", shareCode);

  try {
    await init();
    wasmReady = true;
    showStatus("Ready");
  } catch (error) {
    showStatus("WASM failed to load");
    terminal.writeLine(String(error), "error");
    return;
  }

  const shared = decodeShare();
  const initial = shared || EXAMPLES[0].code;
  examples.value = EXAMPLES[0].name;
  setEditorContent(initial);
  runCheck();
}

boot();
