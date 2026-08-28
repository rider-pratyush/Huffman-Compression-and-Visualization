import os
import struct
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox


APP_DIRECTORY = os.path.dirname(os.path.abspath(__file__))


def executable(name):
    return os.path.join(APP_DIRECTORY, name)


def original_filename(archive_path):
    """Read the safe, display-only filename stored in a HUF1 archive."""
    try:
        with open(archive_path, "rb") as archive:
            header = archive.read(18)
            if len(header) != 18 or header[:4] != b"HUF1" or header[4] != 1:
                raise ValueError("not a HUF1 archive")
            filename_length = struct.unpack("<H", header[16:18])[0]
            filename = archive.read(filename_length).decode("utf-8", errors="replace")
            filename = os.path.basename(filename)
            return filename or "restored_file"
    except (OSError, ValueError, struct.error):
        return "restored_file"


def run_command(command, operation):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode == 0:
        messagebox.showinfo("Success", f"{operation} complete!\n\n{result.stdout}")
    else:
        details = result.stderr or result.stdout or "The operation did not complete."
        messagebox.showerror("Error", f"{operation} failed:\n\n{details}")


def encode_file():
    input_file = filedialog.askopenfilename(
        title="Select a file to archive", filetypes=[("All files", "*.*")]
    )
    if not input_file:
        return
    default_name = os.path.basename(input_file) + ".huf"
    output_file = filedialog.asksaveasfilename(
        title="Save archive as...",
        initialfile=default_name,
        defaultextension=".huf",
        filetypes=[("Huffman archives", "*.huf"), ("All files", "*.*")],
    )
    if output_file:
        run_command([executable("encode.exe"), input_file, output_file], "Archiving")


def decode_file():
    input_file = filedialog.askopenfilename(
        title="Select a HUF1 archive",
        filetypes=[("Huffman archives", "*.huf"), ("All files", "*.*")],
    )
    if not input_file:
        return
    output_file = filedialog.asksaveasfilename(
        title="Restore file as...",
        initialfile=original_filename(input_file),
        filetypes=[("All files", "*.*")],
    )
    if output_file:
        run_command([executable("decode.exe"), input_file, output_file], "Restoring")


root = tk.Tk()
root.title("Huffman File Archiver")
root.geometry("420x230")
root.resizable(False, False)

frame = tk.Frame(root, padx=20, pady=20)
frame.pack(expand=True)

tk.Label(frame, text="Huffman File Archiver", font=("Arial", 16, "bold")).pack(pady=(0, 4))
tk.Label(frame, text="Archive and restore any file type without changing its contents.").pack(pady=(0, 15))
tk.Button(frame, text="Archive a File", command=encode_file, width=25, height=2).pack(pady=5)
tk.Button(frame, text="Restore an Archive", command=decode_file, width=25, height=2).pack(pady=5)

root.mainloop()
