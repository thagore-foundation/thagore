const KEYWORDS = new Set([
  "func", "struct", "impl", "import", "from", "pub", "let", "const", "if", "else", "elif",
  "while", "for", "return", "break", "continue", "include", "as", "and", "or", "not", "in",
]);

const TYPES = new Set(["i32", "i64", "f64", "bool", "str", "void", "ptr"]);
const BUILTINS = new Set(["print", "println", "eprint", "eprintln", "flush"]);
const OPERATORS = new Set(["+", "-", "*", "/", "%", "=", "<", ">", "!", ":", ",", ".", "(", ")", "[", "]"]);

function escapeHtml(text) {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function escapeAttr(text) {
  return escapeHtml(text).replace(/"/g, "&quot;");
}

function diagnosticMap(diagnostics) {
  const map = new Map();
  for (const diagnostic of diagnostics || []) {
    const start = Math.max(0, Number(diagnostic.start || 0));
    const end = Math.max(start + 1, Number(diagnostic.end || start + 1));
    for (let index = start; index < end; index += 1) {
      if (!map.has(index)) {
        map.set(index, diagnostic.message || "error");
      }
    }
  }
  return map;
}

function classifyIdentifier(word) {
  if (KEYWORDS.has(word)) return "kw";
  if (TYPES.has(word)) return "type";
  if (BUILTINS.has(word)) return "builtin";
  return "";
}

function nextToken(source, start) {
  const ch = source[start];
  if (ch === "#") {
    let end = start;
    while (end < source.length && source[end] !== "\n") end += 1;
    return { end, cls: "comment" };
  }
  if (ch === "\"") {
    let end = start + 1;
    let escaped = false;
    while (end < source.length) {
      const current = source[end];
      if (escaped) {
        escaped = false;
      } else if (current === "\\") {
        escaped = true;
      } else if (current === "\"") {
        end += 1;
        break;
      }
      end += 1;
    }
    return { end, cls: "str" };
  }
  if (/\d/.test(ch)) {
    let end = start + 1;
    while (end < source.length && /[\d_]/.test(source[end])) end += 1;
    if (source[end] === ".") {
      end += 1;
      while (end < source.length && /[\d_]/.test(source[end])) end += 1;
    }
    return { end, cls: "num" };
  }
  if (/[A-Za-z_\u00A0-\uFFFF]/.test(ch)) {
    let end = start + 1;
    while (end < source.length && /[A-Za-z0-9_\u00A0-\uFFFF]/.test(source[end])) end += 1;
    return { end, cls: classifyIdentifier(source.slice(start, end)) };
  }
  if (source.startsWith("->", start) || source.startsWith("==", start) || source.startsWith("!=", start)
    || source.startsWith("<=", start) || source.startsWith(">=", start)) {
    return { end: start + 2, cls: "op" };
  }
  if (OPERATORS.has(ch)) {
    return { end: start + 1, cls: "op" };
  }
  return { end: start + 1, cls: "" };
}

export function highlightThagore(source, diagnostics = []) {
  const errors = diagnosticMap(diagnostics);
  let html = "";
  let index = 0;

  while (index < source.length) {
    const { end, cls } = nextToken(source, index);
    const text = source.slice(index, end);
    let className = cls;
    const titles = [];
    for (let cursor = index; cursor < end; cursor += 1) {
      if (errors.has(cursor)) {
        className = className ? `${className} line-error` : "line-error";
        titles.push(errors.get(cursor));
        break;
      }
    }
    const title = titles.length > 0 ? ` title="${escapeAttr(titles[0])}"` : "";
    const escaped = escapeHtml(text);
    html += className ? `<span class="${className}"${title}>${escaped}</span>` : escaped;
    index = end;
  }

  return html || "<br>";
}

export function normalizeEditorText(text) {
  return text.replace(/\r\n/g, "\n").replace(/\u00A0/g, " ");
}
