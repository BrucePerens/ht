#!	/bin/sh
echo "const char gm_build_version[] = \"`git log -n 1 --format=format:%ci`\";" >$1
echo "const char gm_build_number[] = \"`git log -n 1 --format=reference | cut -f 1 -d " "`\";" >>$1
echo "const char gm_build_time[] = \"`date --utc --rfc-3339=seconds`\";" >>$1
