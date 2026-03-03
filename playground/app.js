(() => {
  const sourceEl = document.getElementById("source");
  const outputEl = document.getElementById("output");
  const runBtn = document.getElementById("runBtn");
  const statusEl = document.getElementById("status");

  if (!sourceEl || !outputEl || !runBtn || !statusEl) {
    return;
  }

  const setStatus = (text, cls = "") => {
    statusEl.textContent = text;
    statusEl.className = cls;
  };

  const run = async () => {
    runBtn.disabled = true;
    outputEl.textContent = "";
    setStatus("Running...", "");
    try {
      const response = await fetch("/api/run", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ source: sourceEl.value }),
      });
      const payload = await response.json();
      if (!response.ok || !payload.ok) {
        outputEl.textContent = payload.stderr || payload.error || "run failed";
        setStatus("Failed", "err");
        return;
      }
      const out = [];
      out.push(`exit code: ${payload.exit_code}`);
      if (payload.stdout) {
        out.push("\nstdout:\n" + payload.stdout);
      }
      if (payload.stderr) {
        out.push("\nstderr:\n" + payload.stderr);
      }
      outputEl.textContent = out.join("\n");
      setStatus(`Done in ${payload.elapsed_ms}ms`, "ok");
    } catch (err) {
      outputEl.textContent = String(err);
      setStatus("Error", "err");
    } finally {
      runBtn.disabled = false;
    }
  };

  runBtn.addEventListener("click", run);
})();
