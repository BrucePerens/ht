if test -z "${IDF_PATH}"; then
  . ~/esp/esp-idf/export.sh
fi
chdir $1
shift
idf.py $*
