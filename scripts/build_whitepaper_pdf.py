#!/usr/bin/env python3
import textwrap


def pdf_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def load_text(path: str) -> str:
    lines = []
    in_code = False
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if line.strip().startswith("```"):
                in_code = not in_code
                continue
            if not in_code:
                line = line.lstrip("#").strip()
            lines.append(line)
    return "\n".join(lines)


def wrap_lines(text: str, width: int = 90):
    wrapped = []
    for paragraph in text.split("\n"):
        if not paragraph.strip():
            wrapped.append("")
            continue
        wrapped.extend(textwrap.wrap(paragraph, width=width))
    return wrapped


def build_pdf(lines, output_path: str):
    page_width = 612
    page_height = 792
    margin_left = 72
    margin_top = 720
    line_height = 14
    lines_per_page = int((page_height - 2 * margin_left) / line_height)

    objects = []
    xref = []

    def add_obj(data: str):
        xref.append(sum(len(obj) for obj in objects))
        objects.append(data)

    pages = []
    for i in range(0, len(lines), lines_per_page):
        chunk = lines[i : i + lines_per_page]
        content_lines = [
            "BT",
            "/F1 12 Tf",
            f"{line_height} TL",
            f"{margin_left} {margin_top} Td",
        ]
        for line in chunk:
            content_lines.append(f"({pdf_escape(line)}) Tj")
            content_lines.append("T*")
        content_lines.append("ET")
        content = "\n".join(content_lines)
        content_obj = f"<< /Length {len(content)} >>\nstream\n{content}\nendstream\n"
        add_obj(content_obj)
        content_ref = len(objects)
        page_obj = (
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {page_width} {page_height}] "
            f"/Contents {content_ref} 0 R /Resources << /Font << /F1 4 0 R >> >> >> >>\n"
        )
        add_obj(page_obj)
        pages.append(len(objects))

    add_obj("<< /Type /Catalog /Pages 2 0 R >>\n")
    pages_kids = " ".join(f"{p} 0 R" for p in pages)
    add_obj(f"<< /Type /Pages /Kids [ {pages_kids} ] /Count {len(pages)} >>\n")
    add_obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n")

    # Reorder objects: catalog(1), pages(2), font(3), then page contents/ pages
    # Adjust: we built content/page objects first, so rebuild with new order.
    content_page_objs = objects[:-3]
    catalog = objects[-3]
    pages_obj = objects[-2]
    font = objects[-1]
    objects = [catalog, pages_obj, font] + content_page_objs

    # Rebuild xref with correct offsets.
    xref = []
    offset = 0
    for obj in objects:
        xref.append(offset)
        offset += len(obj)

    with open(output_path, "wb") as handle:
        handle.write(b"%PDF-1.4\n")
        offsets = [handle.tell()]
        for obj in objects:
            offsets.append(handle.tell())
            handle.write(obj.encode("utf-8"))
            handle.write(b"endobj\n")
        xref_pos = handle.tell()
        handle.write(b"xref\n")
        handle.write(f"0 {len(objects)+1}\n".encode("utf-8"))
        handle.write(b"0000000000 65535 f \n")
        for off in offsets[1:]:
            handle.write(f"{off:010d} 00000 n \n".encode("utf-8"))
        handle.write(b"trailer\n")
        handle.write(f"<< /Size {len(objects)+1} /Root 1 0 R >>\n".encode("utf-8"))
        handle.write(b"startxref\n")
        handle.write(f"{xref_pos}\n".encode("utf-8"))
        handle.write(b"%%EOF\n")


def main():
    text = load_text("WHITEPAPER.md")
    lines = wrap_lines(text)
    build_pdf(lines, "whitepaper.pdf")


if __name__ == "__main__":
    main()
