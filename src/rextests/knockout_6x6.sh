#!/bin/bash
declare -a branches=("master" "VCDecompFillin_off" "capture_H_search_off" "mutualFillin_off")

for b in "${branches[@]}"
do
	echo "$b"
	git checkout "$b" >/dev/null 2>/dev/null
	(cd ../../; make >/dev/null 2>/dev/null)
	if [ $? -ne 0 ]
		then
			echo "Make failure!\n"
			break
	fi
	python3 knockout_6x6.py
	echo "\n"
done
git checkout master >/dev/null 2>/dev/null
(cd ../../; make >/dev/null 2>/dev/null)