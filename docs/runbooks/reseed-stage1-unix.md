# Reseed Stage1 Assets on Unix

Mục tiêu: reseed lại asset Stage1 cho `linux` và `macOS` khi pipeline self-host fail do Stage1 cũ hỏng.

## Chính sách

- CI/Release trên Unix chỉ bootstrap từ Stage1 release asset.
- Không dùng fallback Stage0 trên Unix.
- Nếu Stage1 asset hỏng: phải phát hành lại asset Stage1 chuẩn.

## Quy trình ngắn

1. Tạo Stage1 chuẩn trên máy tin cậy (hoặc Windows job có fallback được bật):
   - build `src/thagore.tg` ra `thagore` (Linux/macOS target tương ứng).
2. Probe self-host tối thiểu:
   - `./thagore build examples/hello.tg -o hello_probe`
   - chạy `hello_probe` thành công.
3. Đóng gói seed asset đúng cấu trúc:
   - `bin/thagore`
   - `lib/std/*`
   - nén thành:
     - `thagore-stage1-linux.tar.gz` hoặc
     - `thagore-stage1-macos.tar.gz`
4. Upload asset vào GitHub Release mới nhất (tag đang dùng).
5. Re-run workflow:
   - `ci.yml`
   - `selfhost-matrix.yml`
   - `release.yml`

## Audit bắt buộc trước khi promote seed

Mỗi seed release phải publish thêm:

- `seed-promotion-manifest-*.txt`
- `seed-stage1-provenance-*.json`
- `seed-stage-trace-*.log`

Manifest chứa hash công bố; provenance chứa metadata build + hash artifact + hash stage trace.

### Verify độc lập (user-side)

Ví dụ Linux:

```bash
python3 scripts/stage1_provenance.py verify \
  --provenance bootstrap/seed-stage1-provenance-linux-x86_64.json \
  --manifest bootstrap/seed-promotion-manifest-linux-x86_64.txt \
  --asset bootstrap/thagore-stage1-linux.tar.gz \
  --asset bootstrap/thagore-runtime-linux.a
```

Nếu hash lệch hoặc thiếu entry, script sẽ fail với `CRITICAL`.

## Kiểm tra nhanh

- Stage trace phải hiển thị `bootstrap_source: release_asset_only`.
- Không có lệnh `cmake -S legacy` trong Linux/macOS jobs.
- `bootstrap-seed.yml` phải fail nếu verify manifest/provenance thất bại.
