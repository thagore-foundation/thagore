export class Terminal {
  constructor(el) {
    this.el = el;
    this.lines = [];
  }

  clear() {
    this.el.innerHTML = "";
    this.lines = [];
  }

  writeLine(text, type = "output") {
    const line = document.createElement("div");
    line.className = `term-line term-${type}`;
    line.textContent = text;
    this.lines.push({ text, type });
    this.el.appendChild(line);
    this.el.scrollTop = this.el.scrollHeight;
  }

  writeError(diagnostic) {
    const line = diagnostic.line || 1;
    const col = diagnostic.col || 1;
    this.writeLine(`error: ${diagnostic.message}`, "error");
    this.writeLine(`  --> playground.tg:${line}:${col}`, "error");
  }

  writeRunResult(result, elapsedMs) {
    if (result.success) {
      this.writeLine(`✓ Exited with code 0  (${elapsedMs}ms)`, "success");
    } else {
      this.writeLine(`✗ Runtime error  (${elapsedMs}ms)`, "error");
    }
  }
}
