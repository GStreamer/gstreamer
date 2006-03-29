wget http://www.3gpp.org/ftp/Specs/archive/26_series/26.204/26204-600.zip
unzip 26204-600.zip
unzip 26204-600_ANSI-C_source_code.zip
mv c-code/* .
rm -rf c-code/ 26204-600.zip 26204-600_ANSI-C_source_code.zip
echo "" >> typedef.h # to remove compilation warning (no newline at end of file)
