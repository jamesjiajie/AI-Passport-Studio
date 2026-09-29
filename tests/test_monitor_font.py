from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
source = (root / "main/main.c").read_text(encoding="utf-8")
font = (root / "main/monitor_font.c").read_text(encoding="utf-8")
labels = re.findall(r'"([^"\n]*)"', source)
needed = {ord(char) for label in labels for char in label if ord(char) > 127}
glyphs = {int(value, 16) for value in re.findall(r'/\* U\+([0-9A-F]+) ', font)}
missing = needed - glyphs
assert not missing, f"Chinese UI glyphs missing from monitor_font_16: {''.join(chr(c) for c in sorted(missing))}"
print(f"Monitor font coverage: PASS ({len(needed)} non-ASCII UI glyphs)")
