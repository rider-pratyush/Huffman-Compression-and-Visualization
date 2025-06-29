import tkinter as tk
from tkinter import filedialog, messagebox
import subprocess
import os

def encode_file():
    input_file = filedialog.askopenfilename(title="Select file to compress",filetypes=[("All files", "*.*")])
    if not input_file:
        return
    output_file = filedialog.asksaveasfilename(title="Save as...", defaultextension=".huf")
    if not output_file:
        return

    # Run the C++ encoder executable
    try:
        result = subprocess.run(
            ["encode.exe", input_file, output_file],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            messagebox.showinfo("Success", f"File compressed!\n\n{result.stdout}")
        else:
            messagebox.showerror("Error", f"Compression failed:\n\n{result.stderr}")
    except Exception as e:
        messagebox.showerror("Error", str(e))

def decode_file():
    input_file = filedialog.askopenfilename(title="Select .huf file", filetypes=[("Huffman files", "*.huf")])
    if not input_file:
        return
    output_file = filedialog.asksaveasfilename(title="Save output as...", defaultextension=".txt")
    if not output_file:
        return

    try:
        result = subprocess.run(
            ["decode.exe", input_file, output_file],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            messagebox.showinfo("Success", f"File decompressed!\n\n{result.stdout}")
        else:
            messagebox.showerror("Error", f"Decompression failed:\n\n{result.stderr}")
    except Exception as e:
        messagebox.showerror("Error", str(e))

root = tk.Tk()
root.title("Huffman Coding Drag-and-Drop GUI")
root.geometry("400x200")

frame = tk.Frame(root)
frame.pack(expand=True)

tk.Label(frame, text="Huffman Coding GUI", font=("Arial", 16, "bold")).pack(pady=10)
tk.Button(frame, text="Compress a File", command=encode_file, width=20, height=2).pack(pady=10)
tk.Button(frame, text="Decompress a File", command=decode_file, width=20, height=2).pack(pady=10)

root.mainloop()
