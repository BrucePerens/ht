if test -z "${IDF_PATH}"; then
  echo "Configuring esp-idf"
  export IDF_PATH=~/esp/esp-idf
  . ~/esp/esp-idf/export.sh
fi
chdir $1
shift
python3 ~/esp/esp-idf/tools/idf.py $*
