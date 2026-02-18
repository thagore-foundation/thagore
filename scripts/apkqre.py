import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
DECODED = ROOT / "decoded"
APKSIGNED = ROOT / "apksigned"

JRE_URL = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.9%2B9.1/OpenJDK17U-jre_x64_windows_hotspot_17.0.9_9.zip"
JRE_ZIP = TOOLS / "jre.zip"
JRE_DIR = TOOLS / "jdk"

APKTOOL_URL = "https://bitbucket.org/iBotPeaches/apktool/downloads/apktool_2.9.3.jar"
APKTOOL_JAR = TOOLS / "apktool.jar"

JADX_URL = "https://github.com/skylot/jadx/releases/download/v1.5.3/jadx-1.5.3.zip"
JADX_ZIP = TOOLS / "jadx.zip"
JADX_DIR = TOOLS / "jadx"
JADX_GUI = JADX_DIR / "bin" / "jadx-gui.bat"

SIGNER_URL = "https://github.com/patrickfav/uber-apk-signer/releases/download/v1.3.0/uber-apk-signer-1.3.0.jar"
SIGNER_JAR = TOOLS / "uber-apk-signer.jar"


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def open_path(path: Path) -> None:
    try:
        if hasattr(__import__("os"), "startfile"):
            __import__("os").startfile(str(path))  # type: ignore[attr-defined]
        else:
            subprocess.run(["xdg-open", str(path)], check=False)
    except Exception:
        pass


def run_checked(cmd: list[str], label: str) -> bool:
    proc = subprocess.run(cmd, cwd=ROOT)
    if proc.returncode != 0:
        print(f"{label} failed.")
        return False
    return True


def download_if_missing(url: str, out_path: Path, label: str) -> bool:
    if out_path.exists():
        return True
    print(f"Downloading {label}...")
    try:
        urllib.request.urlretrieve(url, out_path)
    except Exception as exc:
        print(f"Download failed: {label}")
        print(exc)
        return False
    if not out_path.exists():
        print(f"Missing file: {out_path}")
        return False
    return True


def extract_zip(zip_path: Path, out_dir: Path, label: str) -> bool:
    print(f"Extracting {label}...")
    try:
        ensure_dir(out_dir)
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(out_dir)
        return True
    except Exception as exc:
        print(f"Extract {label} failed.")
        print(exc)
        return False


def detect_or_install_java() -> Path | None:
    p1 = JRE_DIR / "jdk-17.0.9+9-jre" / "bin" / "java.exe"
    p2 = JRE_DIR / "bin" / "java.exe"
    if p1.exists():
        return p1
    if p2.exists():
        return p2

    print("Java not found. Downloading Temurin JRE...")
    if not download_if_missing(JRE_URL, JRE_ZIP, "Temurin JRE"):
        return None
    if not extract_zip(JRE_ZIP, JRE_DIR, "JRE"):
        return None
    if p1.exists():
        return p1
    if p2.exists():
        return p2
    return None


def decode_apk(java_bin: Path) -> str | None:
    apk_raw = input("Enter full APK path: ").strip().strip('"')
    apk_path = Path(apk_raw)
    if not apk_path.exists():
        print("File not found.")
        return None

    name = apk_path.stem
    out_dir = DECODED / name
    print("Decoding APK...")
    ok = run_checked(
        [str(java_bin), "-jar", str(APKTOOL_JAR), "d", str(apk_path), "-o", str(out_dir), "-f"],
        "Decode",
    )
    if not ok:
        return None
    open_path(out_dir)
    return name


def build_apk(java_bin: Path, folder: str) -> bool:
    target_dir = DECODED / folder
    if not target_dir.exists():
        print("Folder not found.")
        return False
    out_apk = target_dir / "unsigned.apk"
    print("Building APK...")
    return run_checked(
        [str(java_bin), "-jar", str(APKTOOL_JAR), "b", str(target_dir), "-o", str(out_apk)],
        "Build",
    )


def sign_apk(java_bin: Path, folder: str) -> bool:
    apk_to_sign = DECODED / folder / "unsigned.apk"
    if not apk_to_sign.exists():
        print("unsigned.apk not found. Build first.")
        return False

    output_raw = input("Save signed APK to (default: apksigned): ").strip().strip('"')
    output_dir = Path(output_raw) if output_raw else APKSIGNED
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    ensure_dir(output_dir)

    print("Signing APK...")
    ok = run_checked(
        [
            str(java_bin),
            "-jar",
            str(SIGNER_JAR),
            "--apks",
            str(apk_to_sign),
            "--out",
            str(output_dir),
            "--allowResign",
        ],
        "Signing",
    )
    if not ok:
        return False

    signed_apk = output_dir / "unsigned-aligned-debugSigned.apk"
    final_apk = output_dir / f"{folder}-signed.apk"
    if signed_apk.exists():
        try:
            if final_apk.exists():
                final_apk.unlink()
            shutil.move(str(signed_apk), str(final_apk))
            print(f"Output: {final_apk}")
        except Exception as exc:
            print("Could not rename signed APK.")
            print(exc)
    open_path(output_dir)
    return True


def tools_menu(java_bin: Path, folder: str) -> None:
    while True:
        print(f"=== Working on: {folder} ===")
        print("1. Open folder")
        print("2. Build APK")
        print("3. Sign APK")
        print("4. Open JADX GUI")
        print("5. Back")
        op = input("Choice: ").strip()
        if op == "1":
            open_path(DECODED / folder)
        elif op == "2":
            build_apk(java_bin, folder)
        elif op == "3":
            sign_apk(java_bin, folder)
        elif op == "4":
            if JADX_GUI.exists():
                open_path(JADX_GUI)
            else:
                print("JADX not found.")
        elif op == "5":
            return


def setup_tools() -> Path | None:
    ensure_dir(TOOLS)
    ensure_dir(DECODED)
    ensure_dir(APKSIGNED)

    java_bin = detect_or_install_java()
    if java_bin is None:
        print("Java setup failed.")
        return None

    if not download_if_missing(APKTOOL_URL, APKTOOL_JAR, "Apktool"):
        return None
    if not download_if_missing(SIGNER_URL, SIGNER_JAR, "Uber APK Signer"):
        return None
    if not JADX_GUI.exists():
        if not download_if_missing(JADX_URL, JADX_ZIP, "JADX"):
            return None
        if not extract_zip(JADX_ZIP, JADX_DIR, "JADX"):
            return None
    return java_bin


def main() -> int:
    java_bin = setup_tools()
    if java_bin is None:
        return 1

    while True:
        print("=== ApkQRE (Python) ===")
        print("1. Decode new APK")
        print("2. View decoded")
        print("3. Exit")
        ch = input("Choice: ").strip()
        if ch == "1":
            folder = decode_apk(java_bin)
            if folder:
                tools_menu(java_bin, folder)
        elif ch == "2":
            print("=== Decoded APKs ===")
            for p in sorted(DECODED.iterdir()):
                if p.is_dir():
                    print(p.name)
            folder = input("Folder: ").strip()
            target = DECODED / folder
            if target.exists() and target.is_dir():
                tools_menu(java_bin, folder)
            else:
                print("Folder not found.")
        elif ch == "3":
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
