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

## Kiểm tra nhanh

- Stage trace phải hiển thị `bootstrap_source: release_asset_only`.
- Không có lệnh `cmake -S legacy` trong Linux/macOS jobs.
