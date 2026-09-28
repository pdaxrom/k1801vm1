# C formatting

Use the user's C source/header style for new and modified code, especially
the SERV firmware in `uJ11-fpga/firmware/storage`:

```sh
astyle --style=kr --indent=tab=8 --add-braces --max-code-length=120 *.c *.h
```

This means K&R braces, tabs with width 8, braces around conditional/loop
bodies, and a maximum line length of 120. Do not reformat unrelated files.
`--suffix=none` may be added to avoid formatter backup files.
