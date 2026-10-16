# Register the Excel stand-in (xlstub.dll) as Excel.Application, for apps
# whose "export to Excel" drives Excel through automation (docs/frontends.md,
# "Excel output"). Opt in from an app image:
#   RUN cp /usr/local/share/wine-webapp/excel-stub.sh /etc/wine-webapp/prefix.d/40-excel-stub.sh
# then run wine-webapp-init-prefix as usual. Registered in both registry
# views, so 32-bit and 64-bit programs find it (the DLL itself is 32-bit).
C='{00024500-0000-0000-C000-000000000046}'
for view in 32 64; do
  wine reg add 'HKCR\Excel.Application' /ve /d 'Microsoft Excel Application' /f /reg:$view
  wine reg add 'HKCR\Excel.Application\CLSID' /ve /d "$C" /f /reg:$view
  wine reg add "HKCR\\CLSID\\$C" /ve /d 'Microsoft Excel Application' /f /reg:$view
  wine reg add "HKCR\\CLSID\\$C\\ProgID" /ve /d Excel.Application /f /reg:$view
  wine reg add "HKCR\\CLSID\\$C\\InprocServer32" /ve /d 'Z:\usr\local\lib\wine-webapp\xlstub.dll' /f /reg:$view
  wine reg add "HKCR\\CLSID\\$C\\InprocServer32" /v ThreadingModel /d Apartment /f /reg:$view
done >/dev/null
