// Run after: npm install --prefix tools --no-save lv_font_conv@1.5.3
const fs = require('fs');
const path = require('path');
const {execFileSync} = require('child_process');
const root = path.resolve(__dirname, '..');
const texts = ['ui.c', 'light.c'].map(f => fs.readFileSync(path.join(root, 'main', f), 'utf8')).join('');
// Include every non-ASCII code point, especially full-width punctuation such as U+FF1A.
const symbols = [...new Set([...texts].filter(ch => ch.codePointAt(0) > 0x7f))].sort().join('');
execFileSync(process.execPath, [path.join(__dirname, 'node_modules/lv_font_conv/lv_font_conv.js'),
  '--font', path.join(__dirname, 'font/NotoSansCJKsc-Regular.otf'),
  '--symbols', symbols, '--range', '0x20-0x7e', '--size', '16', '--bpp', '4',
  '--format', 'lvgl', '--no-compress', '--no-kerning', '--lv-font-name', 'ui_font',
  '--lv-fallback', 'lv_font_montserrat_14', '--lv-include', 'lvgl.h',
  '-o', path.join(root, 'main/ui_font.c')], {stdio:'inherit'});
console.log(`Generated ${[...symbols].length} non-ASCII characters plus ASCII; icons use Montserrat fallback.`);
