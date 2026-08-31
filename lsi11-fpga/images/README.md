# Operating-system images

`*.dsk` files are intentionally ignored by Git. For the local verified setup,
`rt11v503.dsk` is copied here and is the default `RT11_IMAGE` used by
`make test-rt11`.

For a fresh checkout, place a compatible raw RK05/SD image here or pass an
absolute path:

```sh
make test-rt11 RT11_IMAGE=/path/to/rt11v503.dsk
```

The FPGA reads the physical SD card as 512-byte logical sectors. The image
must be written to the card without a filesystem wrapper or offset.
