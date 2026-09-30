# Smallest possible app image, used as a smoke test of the base + front end:
# Wine's own 32-bit Notepad (most apps packaged here are 32-bit, and the
# patched Wine DLLs are 32-bit), no files to add.
#
#   docker build -t wine-webapp-example-notepad examples/notepad
#   docker run --rm -p 8080:8080 wine-webapp-example-notepad
#   # open http://localhost:8080
ARG BASE=wine-webapp:vnc
FROM ${BASE}
ENV APP_EXE='C:\windows\syswow64\notepad.exe'
