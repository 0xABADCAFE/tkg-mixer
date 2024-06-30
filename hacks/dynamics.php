<?php

/*
const FACTORS = [
  64.00000000, 32.00000000, 21.33333333, 16.00000000, 12.80000000, 10.66666667, 9.14285714, 8.00000000,
   7.11111111,  6.40000000,  5.81818182,  5.33333333,  4.92307692,  4.57142857, 4.26666667, 4.00000000,
   3.76470588,  3.55555556,  3.36842105,  3.20000000,  3.04761905,  2.90909091, 2.78260870, 2.66666667,
   2.56000000,  2.46153846,  2.37037037,  2.28571429,  2.20689655,  2.13333333, 2.06451613, 2.00000000,
   1.93939394,  1.88235294,  1.82857143,  1.77777778,  1.72972973,  1.68421053, 1.64102564, 1.60000000,
   1.56097561,  1.52380952,  1.48837209,  1.45454545,  1.42222222,  1.39130435, 1.36170213, 1.33333333,
   1.30612245,  1.28000000,  1.25490196,  1.23076923,  1.20754717,  1.18518519, 1.16363636, 1.14285714,
   1.12280702,  1.10344828,  1.08474576,  1.06666667,  1.04918033,  1.03225806, 1.01587302, 1.00000000,
];

$factor_counts = array_fill(0, 64, 0);


for ($i = 1; $i < 32768; ++$i) {
	$factor = $i>>9;
	$factor_counts[$factor]++;
}

print_r($factor_counts);*/

$aTable = [];
$iShift = 2;
for ($i = 0; $i < 64; ++$i) {
	$j = $i + 1;
	if ($j & $i) {
		$aTable[] = (int)(16384.0/$j);
	} else {
		$aTable[] = $iShift++;
	}
}

echo implode(',', $aTable), "\n";