export function encodeShare(source) {
  if (!window.LZString) {
    return `${location.origin}${location.pathname}`;
  }
  const compressed = window.LZString.compressToEncodedURIComponent(source);
  return `${location.origin}${location.pathname}#code=${compressed}`;
}

export function decodeShare() {
  if (!window.LZString) {
    return null;
  }
  const match = location.hash.match(/^#code=(.+)$/);
  if (!match) {
    return null;
  }
  return window.LZString.decompressFromEncodedURIComponent(match[1]);
}
