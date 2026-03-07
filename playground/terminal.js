function escapeHtml(text) {
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

export class Terminal {
  constructor(el) {
    this.el = el;
  }

  clear() {
    this.el.innerHTML = "";
  }

  writeLine(text, type = "out") {
    const line = document.createElement("div");
    line.className = "tline";
    line.innerHTML = `<span class="t-${type}">${escapeHtml(text)}</span>`;
    this.el.appendChild(line);
    this.el.scrollTop = this.el.scrollHeight;
  }

  writeSpacer() {
    const line = document.createElement("div");
    line.className = "tline";
    line.innerHTML = "<span>&nbsp;</span>";
    line.style.height = ".5rem";
    this.el.appendChild(line);
    this.el.scrollTop = this.el.scrollHeight;
  }

  writeError(diagnostic, source = "") {
    const lineNo = Number(diagnostic.line || 1);
    const col = Number(diagnostic.col || 1);
    const endCol = Math.max(col, Number(diagnostic.end_col || col));
    const lines = source.split("\n");
    const sourceLine = lines[lineNo - 1] || "";
    const caretPad = " ".repeat(Math.max(0, col - 1));
    const caretWidth = Math.max(1, endCol - col);
    const gutter = String(lineNo).padStart(2, " ");

    this.writeLine(`error: ${diagnostic.message}`, "err");
    this.writeLine(`  --> playground.tg:${lineNo}:${col}`, "err");
    this.writeLine("   |", "dim");
    this.writeLine(`${gutter} | ${sourceLine}`, "err");
    this.writeLine(`   | ${caretPad}${"^".repeat(caretWidth)}`, "err");
  }

  writeRunResult(success, elapsedMs) {
    if (success) {
      this.writeLine(`✓ Exited with code 0  (${elapsedMs}ms)`, "ok");
    } else {
      this.writeLine(`✗ Run failed  (${elapsedMs}ms)`, "err");
    }
  }

  addCursor() {
    const line = document.createElement("div");
    line.className = "tline";
    line.style.opacity = "1";
    line.innerHTML = "<span class=\"tcursor\"></span>";
    this.el.appendChild(line);
    this.el.scrollTop = this.el.scrollHeight;
  }
}
