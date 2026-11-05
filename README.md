# Chuyên đề 2 — Tinh chỉnh bản đồ truyền qua trong khử sương ảnh

Đề tài so sánh **GIF65** và **FAST2** ở bước tinh chỉnh bản đồ truyền qua trong quy trình khử sương dựa trên **Dark Channel Prior (DCP)**. Hai phương án dùng chung cách ước lượng ánh sáng khí quyển, bản đồ truyền qua thô và bước tái tạo ảnh.

- **GIF65** sử dụng bộ lọc dẫn hướng với cửa sổ 65 × 65.
- **FAST2** thực hiện hai lượt lọc ngang và lọc đệ quy theo chiều dọc với cơ chế nhìn trước một hàng.
- **Thực nghiệm Python** so sánh PSNR, SSIM và thời gian lọc trên SOTS Indoor và Outdoor.
- **Thực nghiệm Vitis HLS** tổng hợp hệ thống khử sương dùng FAST2 và đối chiếu đầu ra RGB với mô hình Python số cố định.

## Cấu trúc thư mục

| Đường dẫn | Nội dung |
|---|---|
| `ipynb_files/CD2_final.ipynb` | Notebook đánh giá GIF65, FAST2 và đối chiếu đầu ra HLS với Python. |
| `hls_src/` | Mã nguồn C/C++, các header và testbench của hệ thống khử sương dùng FAST2. |
| `hls_output/input.ppm` | Ảnh đầu vào Full HD dùng cho testbench. |
| `hls_output/output_hls.ppm` | Ảnh đầu ra HLS của khung thứ hai dùng để đối chiếu. |
| `report/` | Báo cáo cuối kỳ Chuyên đề 2. |
| `data/SOTS/` | Dữ liệu SOTS được tải và sắp xếp theo hướng dẫn bên dưới. |

## 1. Cài đặt môi trường Python

Cài Python 3 và các thư viện cần thiết. Chạy các lệnh sau từ thư mục gốc của repository.

```bash
python -m pip install notebook numpy pandas matplotlib pillow opencv-python scikit-image
python -m notebook
```

Mở `ipynb_files/CD2_final.ipynb` trong Jupyter. Bạn có thể chạy đánh giá SOTS hoặc đối chiếu với ảnh HLS có sẵn ngay trong notebook. Mục 4 hướng dẫn cách tự tạo lại đầu ra HLS và báo cáo tổng hợp.

## 2. Tải và chuẩn bị SOTS

1. Mở [trang tải RESIDE-Standard](https://github.com/Boyiliee/RESIDE-dataset-link/blob/master/RESIDE-Standard.txt).
2. Tìm mục **SOTS (Synthetic Objective Testing Set)** và tải dữ liệu từ liên kết Dropbox hoặc Baidu Yun được cung cấp.
3. Giải nén và sắp xếp hai tập ảnh theo cấu trúc sau.

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

`hazy/` chứa ảnh có sương, còn `gt/` chứa ảnh sạch tham chiếu. Giữ tên ảnh gốc để notebook ghép cặp theo tên trước phần mở rộng hoặc theo mã cảnh trước dấu gạch dưới. Phép đánh giá đầy đủ dùng **500 ảnh có sương cho mỗi tập**.

## 3. Chạy notebook

Trong ô **Cấu hình**, đặt `ROOT` thành đường dẫn tuyệt đối đến thư mục gốc của bản báo cáo, nơi chứa `ipynb_files/`, `hls_src/` và `hls_output/`.

```python
ROOT = Path(r"D:\semester_9\chuyende_2\CD2_final")
```

| Thiết lập | Cách dùng |
|---|---|
| `RUN_SOTS` | Đặt `True` để đánh giá GIF65 và FAST2 trên SOTS. |
| `SAMPLE_LIMIT` | Đặt `None` để chạy đủ 500 ảnh mỗi tập hoặc `10` để chạy thử 10 ảnh mỗi tập. |
| `RUN_HLS_CHECK` | Đặt `True` để đối chiếu HLS với Python. Có thể đặt `RUN_SOTS = False` nếu chỉ thực hiện phép đối chiếu này. |
| `INPUT_PATH`, `HLS_OUTPUT_PATH` | Mặc định đọc `hls_output/input.ppm` và `hls_output/output_hls.ppm` bên dưới `ROOT`. |
| `HLS_FRAME_INDEX` | Giữ `1` khi đối chiếu đầu ra của khung thứ hai do testbench xuất. Chỉ số khung bắt đầu từ 0. |

Chạy các ô theo thứ tự từ phần cấu hình đến hết phần kiểm chứng HLS và benchmark SOTS. Với phép đối chiếu HLS, dùng ảnh đầu vào và đầu ra của cùng một lần thử.

## 4. Chạy mô phỏng và tổng hợp bằng Vitis HLS

### 4.1. Tạo dự án HLS

Tạo một dự án Vitis HLS hoặc một **HLS Component** trong Vitis Unified IDE. Cấu hình theo bảng sau.

| Thông số | Giá trị |
|---|---|
| Hàm top | `dehaze_p7_full_stream` |
| Thiết bị đích | `xc7z020clg400-1` của PYNQ-Z2 |
| Chu kỳ clock mục tiêu | `12.20 ns` |
| Độ phân giải ảnh kiểm thử | `1920 × 1080`, RGB 8 bit mỗi kênh |

Thêm các file theo đúng vai trò.

- **Design Sources** gồm `p7_full_dehaze.cpp`, `p7_dark_core.cpp`, `p7_m5_core.cpp`, `p7_m6_core.cpp` và các header liên quan trong `hls_src/`. Giữ các header cùng thư mục hoặc thêm `hls_src/` vào đường dẫn include nếu cần.
- **Testbench Sources** gồm `tb_p7_full_dehaze.cpp`.

### 4.2. Kiểm tra đường dẫn ảnh trong testbench

Bản testbench của báo cáo dùng hai đường dẫn tuyệt đối sau.

```cpp
static const char* INPUT_PPM =
    "D:/semester_9/chuyende_2/CD2_final/hls_output/input.ppm";

static const char* OUTPUT_PPM =
    "D:/semester_9/chuyende_2/CD2_final/hls_output/output_hls.ppm";
```

Nếu đặt repository ở vị trí khác, sửa hai hằng trên trong **bản sao dùng cho báo cáo** và cập nhật `ROOT` trong notebook tương ứng. Dùng dấu `/` trong chuỗi đường dẫn C++ như ví dụ. Tạo sẵn thư mục `hls_output/` và kiểm tra `input.ppm` là ảnh **PPM P6 nhị phân**, kích thước **1920 × 1080**, giá trị mẫu tối đa **255**.

Khi chạy lại, testbench sẽ ghi đè file tại `OUTPUT_PPM`. Nếu muốn giữ ảnh đầu ra đã dùng trong báo cáo, đặt `OUTPUT_PPM` thành một tên mới trong `hls_output/` và cập nhật `HLS_OUTPUT_PATH` trong notebook để đọc file mới.

### 4.3. Chạy C Simulation

1. Chọn **Run C Simulation**.
2. Testbench đưa cùng một ảnh vào hai lần liên tiếp. Khung đầu dùng để khởi tạo trạng thái ánh sáng khí quyển. Đầu ra của khung thứ hai được ghi vào `OUTPUT_PPM`.
3. Kiểm tra log có dòng `P7 TWO-FRAME STRUCTURAL TEST PASSED`.
4. Mở notebook, bật `RUN_HLS_CHECK = True`, đặt `HLS_FRAME_INDEX = 1` và chạy phần đối chiếu RGB với mô hình Python số cố định.

### 4.4. Chạy C Synthesis

1. Chọn **Run C Synthesis** với hàm top và cấu hình clock đã nêu.
2. Mở **Synthesis Report → Performance & Resource Estimates**.
3. Đọc dòng `dehaze_p7_full_stream` để lấy tài nguyên ước lượng của toàn hệ thống. Mở cây phân cấp và tìm `p7_m6_frame` để lấy tài nguyên ước lượng của riêng khối FAST2.

Tài liệu thao tác với mã thiết kế và testbench có trong [AMD Vitis HLS User Guide – UG1399](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Design-Files-and-Test-Bench-Files).

## 5. Kết quả chính

### Chất lượng ảnh và thời gian lọc trên Python

| Tập thử | Bộ lọc | PSNR trung bình (dB) | SSIM trung bình | Thời gian lọc trung vị (ms/ảnh) |
|---|---|---:|---:|---:|
| SOTS Indoor | GIF65 | 19,77689 | 0,83128 | 94,79 |
| SOTS Indoor | FAST2 | **20,08728** | **0,84094** | **62,26** |
| SOTS Outdoor | GIF65 | **21,29070** | **0,91479** | 81,79 |
| SOTS Outdoor | FAST2 | 20,98415 | 0,91338 | **55,41** |

PSNR và SSIM được tính so với ảnh sạch tham chiếu. Thời gian trong bảng chỉ đo khâu tinh chỉnh bản đồ truyền qua trên Python.

### Kiểm chứng và tài nguyên FAST2

Trên khung Full HD đã kiểm tra, đầu ra HLS và mô hình Python số cố định **trùng khớp cả ba kênh RGB tại 100% điểm ảnh**, với MAE bằng 0.

| Tài nguyên ước lượng từ Vitis HLS | Riêng FAST2 (`p7_m6_frame`) | Toàn hệ thống (`dehaze_p7_full_stream`) |
|---|---:|---:|
| BRAM_18K | 21 | 71 |
| DSP | 12 | 20 |
| FF | 2.594 | 8.920 |
| LUT | 3.610 | 14.478 |

Số liệu toàn hệ thống đã bao gồm khối FAST2. Báo cáo trong `report/` trình bày mô hình bài toán, phân tích độ phức tạp, kiến trúc phần cứng và kết quả thực nghiệm.

## Tài liệu tham khảo

- K. He, J. Sun và X. Tang, [Single Image Haze Removal Using Dark Channel Prior](https://doi.org/10.1109/TPAMI.2010.168).
- K. He, J. Sun và X. Tang, [Guided Image Filtering](https://doi.org/10.1109/TPAMI.2012.213).
- B. Li và cộng sự, [Benchmarking Single-Image Dehazing and Beyond](https://arxiv.org/abs/1712.04143).
