from __future__ import annotations

from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from huffman_codec import CompressionStats, DecompressionStats, compress_file, decompress_file


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    if size < 1024 * 1024:
        return f"{size / 1024:.2f} KB"
    return f"{size / (1024 * 1024):.2f} MB"


class HuffmanApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("哈夫曼文件压缩工具")
        self.geometry("760x460")
        self.minsize(720, 420)

        self.input_file_var = tk.StringVar(value="-")
        self.output_file_var = tk.StringVar(value="-")
        self.input_size_var = tk.StringVar(value="-")
        self.output_size_var = tk.StringVar(value="-")
        self.ratio_var = tk.StringVar(value="-")

        self._build_ui()

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=16)
        root.pack(fill=tk.BOTH, expand=True)

        title = ttk.Label(root, text="文件压缩工具（Huffman）", font=("PingFang SC", 20, "bold"))
        title.pack(anchor=tk.W)

        desc = ttk.Label(
            root,
            text="支持文本文件压缩与解压，自动显示压缩前后大小和压缩率。",
            font=("PingFang SC", 11),
        )
        desc.pack(anchor=tk.W, pady=(6, 14))

        action_row = ttk.Frame(root)
        action_row.pack(fill=tk.X, pady=(0, 14))

        compress_btn = ttk.Button(action_row, text="压缩文本文件", command=self.compress_action)
        compress_btn.pack(side=tk.LEFT, padx=(0, 10))

        decompress_btn = ttk.Button(action_row, text="解压 .huf 文件", command=self.decompress_action)
        decompress_btn.pack(side=tk.LEFT)

        info = ttk.LabelFrame(root, text="结果信息", padding=12)
        info.pack(fill=tk.X)

        self._info_row(info, "输入文件：", self.input_file_var, 0)
        self._info_row(info, "输出文件：", self.output_file_var, 1)
        self._info_row(info, "输入大小：", self.input_size_var, 2)
        self._info_row(info, "输出大小：", self.output_size_var, 3)
        self._info_row(info, "压缩率：", self.ratio_var, 4)

        log_box = ttk.LabelFrame(root, text="运行日志", padding=10)
        log_box.pack(fill=tk.BOTH, expand=True, pady=(14, 0))

        self.log_text = tk.Text(log_box, height=10, wrap=tk.WORD, state=tk.DISABLED, font=("Menlo", 11))
        self.log_text.pack(fill=tk.BOTH, expand=True)

    def _info_row(self, parent: ttk.LabelFrame, label_text: str, value_var: tk.StringVar, row: int) -> None:
        label = ttk.Label(parent, text=label_text, font=("PingFang SC", 11, "bold"))
        label.grid(row=row, column=0, sticky=tk.W, padx=(0, 8), pady=3)
        value = ttk.Label(parent, textvariable=value_var, font=("Menlo", 10))
        value.grid(row=row, column=1, sticky=tk.W, pady=3)

    def _append_log(self, content: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, f"[{timestamp}] {content}\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _show_compression_result(self, stats: CompressionStats) -> None:
        self.input_file_var.set(str(stats.input_path))
        self.output_file_var.set(str(stats.output_path))
        self.input_size_var.set(f"{format_size(stats.original_size)} ({stats.original_size} B)")
        self.output_size_var.set(f"{format_size(stats.compressed_size)} ({stats.compressed_size} B)")
        self.ratio_var.set(
            f"{stats.compression_ratio * 100:.2f}%（节省 {stats.saving_ratio * 100:.2f}%）"
        )

    def _show_decompression_result(self, stats: DecompressionStats) -> None:
        self.input_file_var.set(str(stats.input_path))
        self.output_file_var.set(str(stats.output_path))
        self.input_size_var.set(f"{format_size(stats.compressed_size)} ({stats.compressed_size} B)")
        self.output_size_var.set(f"{format_size(stats.restored_size)} ({stats.restored_size} B)")
        self.ratio_var.set("解压完成（压缩率仅在压缩时计算）")

    def compress_action(self) -> None:
        src = filedialog.askopenfilename(
            title="选择要压缩的文本文件",
            filetypes=[
                ("Text Files", "*.txt *.md *.csv *.log"),
                ("All Files", "*.*"),
            ],
        )
        if not src:
            return

        src_path = Path(src)
        dst = filedialog.asksaveasfilename(
            title="保存压缩文件",
            defaultextension=".huf",
            initialdir=str(src_path.parent),
            initialfile=f"{src_path.name}.huf",
            filetypes=[("Huffman File", "*.huf"), ("All Files", "*.*")],
        )
        if not dst:
            return

        try:
            stats = compress_file(src, dst)
            self._show_compression_result(stats)
            self._append_log(
                f"压缩成功：{stats.input_path.name} -> {stats.output_path.name}，"
                f"压缩率 {stats.compression_ratio * 100:.2f}%"
            )
            messagebox.showinfo("完成", "压缩成功。")
        except Exception as exc:  # noqa: BLE001
            self._append_log(f"压缩失败：{exc}")
            messagebox.showerror("压缩失败", str(exc))

    def decompress_action(self) -> None:
        src = filedialog.askopenfilename(
            title="选择要解压的 .huf 文件",
            filetypes=[("Huffman File", "*.huf"), ("All Files", "*.*")],
        )
        if not src:
            return

        src_path = Path(src)
        default_name = (
            src_path.with_suffix("").name if src_path.suffix.lower() == ".huf" else f"{src_path.name}.decoded.txt"
        )

        dst = filedialog.asksaveasfilename(
            title="保存解压后的文本",
            defaultextension=".txt",
            initialdir=str(src_path.parent),
            initialfile=default_name,
            filetypes=[("Text File", "*.txt"), ("All Files", "*.*")],
        )
        if not dst:
            return

        try:
            stats = decompress_file(src, dst)
            self._show_decompression_result(stats)
            self._append_log(f"解压成功：{stats.input_path.name} -> {stats.output_path.name}")
            messagebox.showinfo("完成", "解压成功。")
        except Exception as exc:  # noqa: BLE001
            self._append_log(f"解压失败：{exc}")
            messagebox.showerror("解压失败", str(exc))


def main() -> None:
    app = HuffmanApp()
    app.mainloop()


if __name__ == "__main__":
    main()
