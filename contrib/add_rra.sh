#!/bin/bash

INPUT=$1
OUTPUT=$2

if [ -z "$INPUT" -o -z "$OUTPUT" ]
then
	cat <<USAGE
Usage: $0 <input> <output>
USAGE
	exit 1
fi

if [ ! -e "$INPUT" ]
then
	echo "No such file: $INPUT"
	exit 1
fi

if [ -e "$OUTPUT" ]
then
	echo "File exists: $OUTPUT"
	exit 1
fi

NUM_DS=0
rrdtool dump "$INPUT" | while read LINE
do
	echo "$LINE"

	if [ "$LINE" = "<ds>" ]
	then
		NUM_DS=$(($NUM_DS + 1))
	fi
	
	if [ "$LINE" = "<!-- Round Robin Archives -->" ]
	then
		for CF in MIN MAX AVERAGE
		do
			cat <<RRA
	<rra>
		<cf> $CF </cf>
		<pdp_per_row> 1 </pdp_per_row>
		<xff> 0.0000000000e+00 </xff>

		<cdp_prep>
RRA
			for ((i=0; i < $NUM_DS; i++))
			do
				echo "			<ds><value> NaN </value>  <unknown_datapoints> 1 </unknown_datapoints></ds>"
			done
			echo "		</cdp_prep>"
			echo "		<database>"

			DS_VALUES=`for ((i=0; i < $NUM_DS; i++)); do echo -n "<v> NaN </v>"; done`
			for ((i=0; i < 2200; i++))
			do
				echo "			<!-- $i --> <row>$DS_VALUES</row>"
			done
			echo "		</database>"
			echo "	</rra>"
		done
	fi
done >"$OUTPUT.xml"

rrdtool restore "$OUTPUT.xml" "$OUTPUT" -r >/dev/null
rm -f "$OUTPUT.xml"
