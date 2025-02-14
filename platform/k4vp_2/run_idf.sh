if test -z "${IDF_PATH}"; then
  export IDF_PATH=~/esp/esp-idf
  . ~/esp/esp-idf/export.sh
fi
chdir $1
shift
idf.py $*
