# Ký số bộ cài Windows

Để bộ cài `.exe` không bị cảnh báo đỏ nặng trên Windows, cần ký số file phát hành.

## Secret cần cấu hình trên GitHub

- `WINDOWS_SIGN_CERT_BASE64`: nội dung file `.pfx` đã Base64.
- `WINDOWS_SIGN_CERT_PASSWORD`: mật khẩu của `.pfx`.

## Hành vi workflow

- Nếu **không có** 2 secret trên: workflow vẫn build bình thường, **bỏ qua ký số**.
- Nếu **có đủ** secret: workflow tự ký các file `.exe` trên lane Windows:
  - `dist/bin/thagore.exe`
  - `dist/bin/thag.exe`
  - `thagore-setup-windows-<arch>.exe`
  - `thagore-stage1-windows-<arch>.exe` (asset seed lane tương ứng)

## Lưu ý SmartScreen

- Ký số giúp giảm cảnh báo, nhưng SmartScreen còn phụ thuộc reputation.
- Chứng thư EV và số lượng tải/cài đặt thực tế sẽ cải thiện cảnh báo nhanh hơn.
