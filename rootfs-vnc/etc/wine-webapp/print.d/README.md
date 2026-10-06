# Print steps

Executables here run on every print, in name order, between the PDF printer
and the browser's Prints list. An app image adds its own (`COPY 50-myapp
/etc/wine-webapp/print.d/`). Contract: `wine-webapp-print-hooks` in
`/usr/local/bin`, and `docs/frontends.md`, "Printing".
