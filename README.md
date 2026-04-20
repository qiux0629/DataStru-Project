# 文件压缩工具（Huffman）

基于哈夫曼编码算法实现的文本文件压缩与解压工具，包含图形界面。

## 功能

- 哈夫曼树构建（最小堆）
- 规范化哈夫曼编码（Canonical Huffman）优化编码表
- 文本文件压缩与解压（`.huf` 自定义格式）
- GUI 展示压缩前后大小和压缩率

## 运行环境

- Python 3.10+
- 标准库 `tkinter`（macOS / Windows 默认自带）

## 使用方式

### 1. 启动图形界面

```bash
python3 app.py
```

### 2. 命令行（可选）

压缩：

```bash
python3 huffman_codec.py compress 输入文件.txt -o 输出文件.huf
```

解压：

```bash
python3 huffman_codec.py decompress 输入文件.huf -o 输出文件.txt
```

## 文件格式说明（`.huf`）

- 4 字节魔数：`HUF1`
- 原始大小：8 字节（无符号整数）
- 符号数：2 字节
- 编码长度表：`symbol(1B) + length(1B)` × N
- 位流总位数：8 字节
- 压缩位流数据

## 项目结构

- `huffman_codec.py`：哈夫曼编码/解码核心逻辑
- `app.py`：Tkinter 图形界面
