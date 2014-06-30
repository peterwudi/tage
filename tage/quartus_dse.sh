PF="bpredTop.qpf"
TM="bpredTop"

for seed in 0 1 2 3 4
do
	echo "Running with seed $seed..."
	quartus_map $PF
	quartus_fit $PF --seed $seed
	quartus_sta $PF

	#mv output_files/$TM.map.rpt output_files/$seed.$TM.map.rpt
	mv output_files/$TM.fit.rpt output_files/$seed.$TM.fit.rpt
	mv output_files/$TM.sta.rpt output_files/$seed.$TM.sta.rpt
done
