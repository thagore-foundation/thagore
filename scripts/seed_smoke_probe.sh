#!/usr/bin/env bash

resolve_probe_bin() {
  local stem="$1"
  local src="${2:-}"
  local base=""
  if [[ -n "$src" ]]; then
    base="$(basename "$src" .tg)"
  fi
  for c in "$stem" "${stem}.exe" "${stem%.exe}" "$base" "${base}.exe" "examples/${base}" "examples/${base}.exe" "src/${base}" "src/${base}.exe"; do
    if [[ -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  local discovered=""
  discovered="$(
    {
      find . -maxdepth 4 -type f -name "${stem}*" 2>/dev/null
      if [[ -n "$base" ]]; then
        find . -maxdepth 4 -type f -name "${base}*" 2>/dev/null
        find . -maxdepth 4 -type f -name "*${base}*_0_41*" 2>/dev/null
        find . -maxdepth 4 -type f -name "*${base}*.exe" 2>/dev/null
      fi
    } | sed 's|^\./||' | grep -Ev '\.(tg|ll|o|obj|txt|log|json|md|yml|yaml|a|lib)$' | head -n1 || true
  )"
  if [[ -n "$discovered" ]]; then
    echo "$discovered"
    return 0
  fi
  return 1
}

resolve_probe_ir() {
  local stem="$1"
  local src="${2:-}"
  local base=""
  if [[ -n "$src" ]]; then
    base="$(basename "$src" .tg)"
  fi
  for c in "${stem}.ll" "${stem%.exe}.ll" "${base}.ll" "examples/${base}.ll" "src/${base}.ll"; do
    if [[ -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  local discovered=""
  discovered="$(
    {
      find . -maxdepth 4 -type f -name "${stem}*.ll" 2>/dev/null
      if [[ -n "$base" ]]; then
        find . -maxdepth 4 -type f -name "${base}*.ll" 2>/dev/null
        find . -maxdepth 4 -type f -name "*${base}*.ll" 2>/dev/null
      fi
    } | head -n1 || true
  )"
  if [[ -n "$discovered" ]]; then
    echo "$discovered"
    return 0
  fi
  return 1
}

link_probe_ir() {
  local ir="$1"
  local out="$2"
  if [[ "$RUNNER_OS_NAME" == "Windows" ]]; then
    clang "$ir" -o "$out" -Wno-override-module thag_runtime.lib >/dev/null 2>&1 && return 0
    clang "$ir" -o "$out" -Wno-override-module libthag_runtime.a >/dev/null 2>&1 && return 0
    return 1
  fi
  clang "$ir" -o "$out" -Wno-override-module libthag_runtime.a >/dev/null 2>&1 && return 0
  clang "$ir" -o "$out" -Wno-override-module thag_runtime.lib >/dev/null 2>&1 && return 0
  return 1
}

seed_smoke_probe() {
  if [[ "$MATRIX_ASSET_TAG" == "linux-arm64" ]]; then
    echo "[SEED] skip early smoke probe on linux-arm64 (kept in verify stage)." | tee -a "$STAGE_TRACE_FILE"
    return 0
  fi
  run_seed_compiler() {
    local cc="$1"
    shift
    local helper_bin=""
    if [[ -n "${HELPER_COMPANION_BIN:-}" && -f "${HELPER_COMPANION_BIN#./}" ]]; then
      helper_bin="$HELPER_COMPANION_BIN"
    fi
    if [[ -n "$helper_bin" ]]; then
      if env THAG_HELPER_BIN="$helper_bin" "./$cc" "$@"; then
        return 0
      fi
    else
      if "./$cc" "$@"; then
        return 0
      fi
    fi
    local rc=$?
    if [[ -n "$helper_bin" ]]; then
      if env THAG_HELPER_BIN="$helper_bin" THAG_ALLOW_SELF_HELPER=1 "./$cc" "$@"; then
        if [[ "${seed_helper_fallback_used:-0}" == "0" ]]; then
          echo "[SEED][probe][compat] $CURRENT_TAG: enabled THAG_ALLOW_SELF_HELPER=1 fallback with helper=$helper_bin for bootstrap probe." | tee -a "$STAGE_TRACE_FILE"
          seed_helper_fallback_used=1
        fi
        return 0
      fi
      return $rc
    fi
    if THAG_ALLOW_SELF_HELPER=1 "./$cc" "$@"; then
      if [[ "${seed_helper_fallback_used:-0}" == "0" ]]; then
        echo "[SEED][probe][compat] $CURRENT_TAG: enabled THAG_ALLOW_SELF_HELPER=1 fallback for bootstrap probe." | tee -a "$STAGE_TRACE_FILE"
        seed_helper_fallback_used=1
      fi
      return 0
    fi
    return $rc
  }
  hash_file() {
    local f="$1"
    if command -v sha256sum >/dev/null 2>&1; then
      sha256sum "$f" | awk '{print $1}'
      return 0
    fi
    if command -v shasum >/dev/null 2>&1; then
      shasum -a 256 "$f" | awk '{print $1}'
      return 0
    fi
    echo ""
    return 0
  }
  local cc_bin="stage1"
  local probe_src="__seed_probe_min__.tg"
  local out_stem="_seed_gate_hello"
  local out_bin=""
  local out_ir=""
  local stage2_stem="_seed_gate_stage2"
  local stage2_bin=""
  local stage2_ir=""
  local cc_sig_before=""
  local cc_sig_after=""
  local got=""
  local seed_helper_fallback_used=0
  local probe_build_log="_seed_probe_build.log"
  local probe_emit_log="_seed_probe_emit.log"
  local stage2_build_log="_seed_probe_stage2_build.log"
  local stage2_emit_log="_seed_probe_stage2_emit.log"
  local probe_exec_mode=1
  printf 'func main() -> i32:\n    return 0\n' > "$probe_src"
  rm -f \
    _seed_gate_hello _seed_gate_hello.exe _seed_gate_hello.ll \
    _seed_gate_stage2 _seed_gate_stage2.exe \
    hello hello.exe hello.ll hello_0_41.ll \
    thagore thagore.exe thagore.ll src/thagore.ll \
    "$probe_build_log" "$probe_emit_log" "$stage2_build_log" "$stage2_emit_log" \
    __seed_probe_missing__* \
    || true
  if [[ "$RUNNER_OS_NAME" == "Windows" ]]; then
    cc_bin="stage1.exe"
  fi
  run_seed_compiler "$cc_bin" --version >/dev/null 2>&1 || {
    echo "[SEED][probe] $CURRENT_TAG: '$cc_bin --version' failed for $MATRIX_ASSET_TAG" | tee -a "$STAGE_TRACE_FILE"
    return 1
  }
  if run_seed_compiler "$cc_bin" build __seed_probe_missing__.tg -o __seed_probe_missing__.out >/dev/null 2>&1; then
    if [[ -f "__seed_probe_missing__.out" || -f "__seed_probe_missing__.out.exe" ]]; then
      echo "[SEED][probe] $CURRENT_TAG: compiler produced output for missing input (hard no-op behavior), reject bootstrap tag." | tee -a "$STAGE_TRACE_FILE"
      return 1
    fi
    echo "[SEED][probe][WARN] $CURRENT_TAG: compiler returned success for missing input, but produced no output. Continue with strict positive probes." | tee -a "$STAGE_TRACE_FILE"
  fi
  run_seed_compiler "$cc_bin" build "$probe_src" -o "$out_stem" >"$probe_build_log" 2>&1 || true
  out_bin="$(resolve_probe_bin "$out_stem" "$probe_src" || true)"
  if [[ -z "$out_bin" ]]; then
    run_seed_compiler "$cc_bin" --emit-llvm-internal "$probe_src" -o "${out_stem}.ll" >"$probe_emit_log" 2>&1 || true
    out_ir="$(resolve_probe_ir "$out_stem" "$probe_src" || true)"
    if [[ -n "$out_ir" && -f "$out_ir" ]]; then
      if [[ "$RUNNER_OS_NAME" == "Windows" ]]; then
        link_probe_ir "$out_ir" "${out_stem}.exe" || return 1
      else
        link_probe_ir "$out_ir" "${out_stem}" || return 1
      fi
      out_bin="$(resolve_probe_bin "$out_stem" "$probe_src" || true)"
    fi
  fi
  if [[ -z "$out_bin" || ! -f "$out_bin" ]]; then
    if [[ -n "$out_ir" && -f "$out_ir" ]]; then
      echo "[SEED][probe][WARN] $CURRENT_TAG: no probe executable, but IR was emitted ($out_ir). Continue and enforce strict executable gates in verify stage." | tee -a "$STAGE_TRACE_FILE"
      probe_exec_mode=0
    else
      echo "[SEED][probe] $CURRENT_TAG: failed to emit probe artifact for $MATRIX_ASSET_TAG" | tee -a "$STAGE_TRACE_FILE"
      if [[ -f "$probe_build_log" ]]; then
        echo "[SEED][probe] $CURRENT_TAG: build log tail:" | tee -a "$STAGE_TRACE_FILE"
        tail -n 40 "$probe_build_log" | sed 's/^/[seed-probe-build] /' | tee -a "$STAGE_TRACE_FILE" || true
      fi
      if [[ -f "$probe_emit_log" ]]; then
        echo "[SEED][probe] $CURRENT_TAG: emit log tail:" | tee -a "$STAGE_TRACE_FILE"
        tail -n 40 "$probe_emit_log" | sed 's/^/[seed-probe-emit] /' | tee -a "$STAGE_TRACE_FILE" || true
      fi
      return 1
    fi
  fi
  if [[ "$probe_exec_mode" == "1" ]]; then
    chmod +x "$out_bin" || true
    got="$("./$out_bin" | tr -d '\r')"
    if [[ -n "$got" ]]; then
      echo "[SEED][probe] $CURRENT_TAG: minimal probe output mismatch for $MATRIX_ASSET_TAG: '$got'" | tee -a "$STAGE_TRACE_FILE"
      return 1
    fi
  fi
  cc_sig_before="$(hash_file "$cc_bin" || true)"
  run_seed_compiler "$cc_bin" build src/thagore.tg -o "$stage2_stem" >"$stage2_build_log" 2>&1 || true
  stage2_bin="$(resolve_probe_bin "$stage2_stem" "src/thagore.tg" || true)"
  if [[ -z "$stage2_bin" || ! -f "$stage2_bin" ]]; then
    run_seed_compiler "$cc_bin" --emit-llvm-internal src/thagore.tg -o "${stage2_stem}.ll" >"$stage2_emit_log" 2>&1 || true
    stage2_ir="$(resolve_probe_ir "$stage2_stem" "src/thagore.tg" || true)"
    if [[ -z "$stage2_ir" || ! -f "$stage2_ir" ]]; then
      cc_sig_after="$(hash_file "$cc_bin" || true)"
      echo "[SEED][probe] $CURRENT_TAG: stage2 artifact missing (hash_before=$cc_sig_before hash_after=$cc_sig_after)." | tee -a "$STAGE_TRACE_FILE"
      echo "[SEED][probe] $CURRENT_TAG: stage2 artifact missing for $MATRIX_ASSET_TAG" | tee -a "$STAGE_TRACE_FILE"
      if [[ -f "$stage2_build_log" ]]; then
        echo "[SEED][probe] $CURRENT_TAG: stage2 build log tail:" | tee -a "$STAGE_TRACE_FILE"
        tail -n 40 "$stage2_build_log" | sed 's/^/[seed-probe-stage2-build] /' | tee -a "$STAGE_TRACE_FILE" || true
      fi
      if [[ -f "$stage2_emit_log" ]]; then
        echo "[SEED][probe] $CURRENT_TAG: stage2 emit log tail:" | tee -a "$STAGE_TRACE_FILE"
        tail -n 40 "$stage2_emit_log" | sed 's/^/[seed-probe-stage2-emit] /' | tee -a "$STAGE_TRACE_FILE" || true
      fi
      return 1
    fi
    echo "[SEED][probe][WARN] $CURRENT_TAG: stage2 executable missing for $MATRIX_ASSET_TAG, but IR was emitted ($stage2_ir). Continue to strict verify stage." | tee -a "$STAGE_TRACE_FILE"
    return 0
  fi
  run_seed_compiler "$stage2_bin" --version >/dev/null 2>&1 || {
    echo "[SEED][probe] $CURRENT_TAG: stage2 --version failed for $MATRIX_ASSET_TAG" | tee -a "$STAGE_TRACE_FILE"
    return 1
  }
  return 0
}
