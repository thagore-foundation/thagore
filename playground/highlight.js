const PRIMARY_KEYWORDS = new Set([
  "intent", "flow", "saga", "step", "compensate", "with", "parallel", "hint", "goal",
  "actor", "receive", "state", "asm", "volatile", "defer", "spawn", "impl", "type",
]);

const SECONDARY_KEYWORDS = new Set([
  "func", "let", "const", "struct", "import", "from", "pub", "if", "else", "elif", "while",
  "for", "return", "break", "continue", "include", "as", "and", "or", "not", "in", "true",
  "false",
]);

const BUILTINS = new Set([
  "print", "println", "eprint", "eprintln", "flush", "sqrt", "pow", "gcd", "len", "replace",
  "to_upper", "to_lower", "from_int", "from_f64",
]);

const TYPES = new Set([
  "i32", "i64", "f64", "bool", "str", "void", "ptr", "Int", "Float", "Float64", "USize",
  "List", "String",
]);

const GOAL_WORDS = new Set([
  "minimize_time", "nearly_sorted", "auto", "precision", "on_success", "on_fail", "bound",
]);

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

function nextToken(source, start) {
  const ch = source[start];
  if (ch === "#") {
    let end = start;
    while (end < source.length && source[end] !== "\n") {
      end += 1;
    }
    return { end, kind: "comment" };
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
    return { end, kind: "string" };
  }
  if (/\d/.test(ch)) {
    let end = start + 1;
    while (end < source.length && /[\d_]/.test(source[end])) {
      end += 1;
    }
    if (source[end] === ".") {
      end += 1;
      while (end < source.length && /[\d_]/.test(source[end])) {
        end += 1;
      }
    }
    return { end, kind: "number" };
  }
  if (/[A-Za-z_\u00A0-\uFFFF]/.test(ch)) {
    let end = start + 1;
    while (end < source.length && /[A-Za-z0-9_\u00A0-\uFFFF]/.test(source[end])) {
      end += 1;
    }
    return { end, kind: "ident" };
  }
  if (
    source.startsWith("->", start)
    || source.startsWith("==", start)
    || source.startsWith("!=", start)
    || source.startsWith("<=", start)
    || source.startsWith(">=", start)
  ) {
    return { end: start + 2, kind: "operator" };
  }
  if ("+-*/%=<>!:,.()[]{}".includes(ch)) {
    return { end: start + 1, kind: "operator" };
  }
  return { end: start + 1, kind: "plain" };
}

function classifyIdentifier(word, previousWord) {
  if (previousWord === "func") {
    return "tk-fn";
  }
  if (PRIMARY_KEYWORDS.has(word)) {
    return "tk-kw";
  }
  if (SECONDARY_KEYWORDS.has(word)) {
    return "tk-kw2";
  }
  if (BUILTINS.has(word)) {
    return "tk-bi";
  }
  if (TYPES.has(word)) {
    return "tk-ty";
  }
  if (GOAL_WORDS.has(word)) {
    return "tk-goal";
  }
  return "";
}

export function highlightThagore(source, diagnostics = []) {
  const errors = diagnosticMap(diagnostics);
  let html = "";
  let index = 0;
  let previousWord = "";

  while (index < source.length) {
    const { end, kind } = nextToken(source, index);
    const text = source.slice(index, end);
    let className = "";

    if (kind === "comment") {
      className = "tk-cmt";
    } else if (kind === "string") {
      className = "tk-str";
    } else if (kind === "number") {
      className = "tk-num";
    } else if (kind === "operator") {
      className = "tk-op";
    } else if (kind === "ident") {
      className = classifyIdentifier(text, previousWord);
      previousWord = text;
    } else if (text.trim() !== "") {
      previousWord = "";
    }

    if (kind !== "ident" && text.trim() !== "") {
      previousWord = kind === "operator" ? previousWord : "";
    }

    const titleMessages = [];
    for (let cursor = index; cursor < end; cursor += 1) {
      if (errors.has(cursor)) {
        className = className ? `${className} line-error` : "line-error";
        titleMessages.push(errors.get(cursor));
        break;
      }
    }

    const title = titleMessages.length > 0 ? ` title="${escapeAttr(titleMessages[0])}"` : "";
    const escaped = escapeHtml(text);
    html += className ? `<span class="${className}"${title}>${escaped}</span>` : escaped;
    index = end;
  }

  return html || "<br>";
}

export function normalizeEditorText(text) {
  return text.replace(/\r\n/g, "\n").replace(/\u00A0/g, " ");
}
