"""Reject old board drivers and GPIO literals outside the single board table."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
pin_header = root / 'main/include/hardware/fami32_pin.h'
errors = []
legacy = re.compile(r'PCM5102|MPR121|mpr121|TOUCHPAD_(?:SDA|SCL|MAP|[01]_ADDRS)|KEYPAD_[RC][0-9]|LED_GPIO')
literals = [
    re.compile(r'\bGPIO_NUM_\d+\b'),
    re.compile(r'(?:static_cast<gpio_num_t>\s*\(|\(gpio_num_t\)\s*)\d+'),
    re.compile(r'\b(?:gpio_set_level|gpio_get_level|gpio_set_direction|gpio_reset_pin|pinMode|digitalWrite|digitalRead)\s*\(\s*\d+'),
    re.compile(r'\.(?:\w*gpio\w*|\w*io_num|mclk|bclk|ws|dout|din)\s*=\s*\d+\b'),
    re.compile(r'#define\s+\w*(?:GPIO|_PIN)\w*\s+\d+\b'),
]
for path in sorted((root / 'main').rglob('*')):
    if path.suffix not in {'.c', '.cpp', '.h'} and path.name != 'CMakeLists.txt':
        continue
    source = path.read_text(encoding='utf-8')
    # Preserve line numbers while ignoring comments.
    source = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: '\n' * m[0].count('\n'), source, flags=re.S)
    for line_no, line in enumerate(source.splitlines(), 1):
        if legacy.search(line):
            errors.append(f'{path.relative_to(root)}:{line_no}: legacy hardware reference')
        if path != pin_header and any(pattern.search(line) for pattern in literals):
            errors.append(f'{path.relative_to(root)}:{line_no}: GPIO literal outside fami32_pin.h')

if errors:
    sys.exit('\n'.join(errors))
print('Hardware source check passed: no legacy board references or scattered GPIO literals.')
