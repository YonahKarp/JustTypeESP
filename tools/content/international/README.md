# International glyph package

The binary files are generated from Noto Sans Regular (SIL Open Font License;
see `OFL.txt`) using the repository's `tools/font-build` LVGL font converter.
With `tools/font-build/NotoSans-Regular.ttf` present, regenerate each size with:

```powershell
foreach ($size in 14,16,18,20,24,28) {
  node tools/font-build/node_modules/lv_font_conv/lv_font_conv.js --font tools/font-build/NotoSans-Regular.ttf --size $size --bpp 2 --format bin --no-compress --no-kerning -r 0xA0-0xFF,0x400-0x4FF,0x20AC,0x2116 --output "tools/content/international/$size.bin"
}
```

The package builder copies these files into the versioned download catalog.
Change the package version if their bytes change after publication.
