# Chuyên đề 2 — Tinh chỉnh bản đồ truyền qua trong khử sương ảnh

Đề tài so sánh **GIF65** và **FAST2** ở bước tinh chỉnh bản đồ truyền qua trong quy trình khử sương dựa trên **Dark Channel Prior (DCP)**. Hai phương án dùng chung bản đồ truyền qua thô và bước tái tạo ảnh để đánh giá ảnh hưởng của riêng bộ lọc tinh chỉnh.

- **GIF65** sử dụng bộ lọc dẫn hướng với cửa sổ 65 × 65.
- **FAST2** sử dụng hai lượt lọc ngang và lọc đệ quy theo chiều dọc với cơ chế nhìn trước một hàng.
- **Đánh giá trên Python** gồm PSNR, SSIM và thời gian thực thi riêng của khâu lọc trên SOTS Indoor và Outdoor.
- **Đánh giá phần cứng** gồm tài nguyên ước lượng sau tổng hợp FAST2 bằng Vitis HLS và đối chiếu đầu ra RGB với mô hình Python số cố định.

## Cấu trúc thư mục

| Thư mục | Nội dung |
|---|---|
| `ipynb_files/` | Notebook mô phỏng và đánh giá GIF65, FAST2. |
| `hls_output/` | Ảnh đầu vào và đầu ra dùng để đối chiếu HLS với Python. |
| `report/` | Báo cáo cuối kỳ Chuyên đề 2. |
| `data/` | Thư mục dữ liệu SOTS cần chuẩn bị theo hướng dẫn bên dưới. |

## 1. Cài đặt môi trường

Cần Python 3 và các thư viện sau. Chạy lệnh trong thư mục gốc của repository.

```bash
python -m pip install notebook numpy pandas matplotlib pillow opencv-python scikit-image
python -m notebook
```

Trong Jupyter, mở notebook đánh giá trong thư mục `ipynb_files/`.

## 2. Tải và chuẩn bị dataset SOTS

1. Mở [trang tải RESIDE-Standard](https://github.com/Boyiliee/RESIDE-dataset-link/blob/master/RESIDE-Standard.txt).
2. Tìm mục **SOTS (Synthetic Objective Testing Set)** và tải dữ liệu từ liên kết Dropbox hoặc Baidu Yun.
3. Giải nén và sắp xếp dữ liệu theo cấu trúc sau.

```text
data/
└── SOTS/
    ├── indoor/
    │   ├── hazy/
    │   └── gt/
    └── outdoor/
        ├── hazy/
        └── gt/
```

`hazy/` chứa ảnh có sương và `gt/` chứa ảnh sạch tham chiếu. Giữ tên gốc khi giải nén. Notebook ghép ảnh theo tên không có phần mở rộng hoặc theo mã cảnh đứng trước dấu gạch dưới trong tên ảnh có sương. Phép đánh giá đầy đủ dùng **500 ảnh có sương cho mỗi tập**.

## 3. Cấu hình và chạy notebook

Ở ô **Cấu hình**, sửa `ROOT` thành đường dẫn tuyệt đối tới thư mục gốc của repository, tức thư mục chứa `ipynb_files/` và `hls_output/`.

```python
ROOT = Path(r"C:\duong-dan\toi\thu-muc-du-an")
```

| Thiết lập | Giá trị và mục đích |
|---|---|
| `RUN_SOTS` | Đặt `True` để chạy đánh giá GIF65 và FAST2 trên SOTS. |
| `SAMPLE_LIMIT` | Đặt `None` để chạy đủ 500 ảnh mỗi tập. Đặt `10` để kiểm tra nhanh 10 ảnh mỗi tập. |
| `RUN_HLS_CHECK` | Đặt `True` để đối chiếu ảnh HLS với Python, hoặc `False` nếu chỉ chạy SOTS. |
| `INPUT_PATH`, `HLS_OUTPUT_PATH` | Trỏ đến ảnh đầu vào và đầu ra tương ứng. Mặc định là `hls_output/input.ppm` và `hls_output/output_hls.ppm`. |

Chạy các ô theo thứ tự từ trên xuống, từ phần cấu hình đến phần **Benchmark SOTS**. Để đối chiếu HLS, dùng đúng cặp ảnh của cùng lần thử và giữ `HLS_FRAME_INDEX` phù hợp với đầu ra đã xuất. Trong phép thử báo cáo, `HLS_FRAME_INDEX = 1` tương ứng với khung thứ hai khi đưa cùng ảnh vào hai lần.

## 4. Kết quả chính

### Chất lượng ảnh và thời gian lọc trên Python

| Tập thử | Bộ lọc | PSNR trung bình (dB) | SSIM trung bình | Thời gian lọc trung vị (ms/ảnh) |
|---|---|---:|---:|---:|
| SOTS Indoor | GIF65 | 19,77689 | 0,83128 | 94,79 |
| SOTS Indoor | FAST2 | **20,08728** | **0,84094** | **62,26** |
| SOTS Outdoor | GIF65 | **21,29070** | **0,91479** | 81,79 |
| SOTS Outdoor | FAST2 | 20,98415 | 0,91338 | **55,41** |

PSNR và SSIM được tính so với ảnh sạch tham chiếu. Thời gian trong bảng chỉ đo khâu tinh chỉnh bản đồ truyền qua trên Python.

### Kiểm chứng FAST2 và tài nguyên FPGA

Trên khung ảnh 1920 × 1080 đã kiểm tra, đầu ra RGB của FAST2 từ HLS trùng khớp với mô hình Python số cố định tại **100% điểm ảnh**, với MAE bằng 0.

| Tài nguyên ước lượng từ Vitis HLS | Riêng khối FAST2 | Toàn bộ hệ thống khử sương |
|---|---:|---:|
| BRAM_18K | 21 | 71 |
| DSP | 12 | 20 |
| FF | 2.594 | 8.920 |
| LUT | 3.610 | 14.478 |

Số liệu toàn hệ thống đã bao gồm khối FAST2. Báo cáo trong `report/` trình bày mô hình bài toán, phân tích độ phức tạp, kiến trúc bộ lọc và kết quả thực nghiệm.

## Tài liệu tham khảo

- K. He, J. Sun và X. Tang, [Single Image Haze Removal Using Dark Channel Prior](https://doi.org/10.1109/TPAMI.2010.168).
- K. He, J. Sun và X. Tang, [Guided Image Filtering](https://doi.org/10.1109/TPAMI.2012.213).
- B. Li và cộng sự, [Benchmarking Single-Image Dehazing and Beyond](https://arxiv.org/abs/1712.04143).
