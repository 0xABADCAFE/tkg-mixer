<?php

declare(strict_types=1);

function print_table(array $aTable, int $iMax) {
    echo
        "+------+---------+-------+\n",
        "| Line |  Access | Hit % |\n",
        "+------+---------+-------+\n";
    foreach ($aTable as $i => $iValue) {
        $fPercent = (100.0*$iValue)/$iMax;
        printf("| %4d | %7d | %5.2f | \n", $i, $iValue, $fPercent);
    }
    echo "+------+---------+-------+\n";
}

$sSampleData = file_get_contents("linear.raw");
$iLength = strlen($sSampleData);

echo "Read ", $iLength, " values, checking distribution\n";


echo "Linear:\n";
$aHitTable = array_fill(0,256,0);
for ($i = 0; $i < $iLength; ++$i) {
    $iValue = ord($sSampleData[$i]);
    $aHitTable[$iValue]++;
}
$aLineHitRate = array_map('array_sum', array_chunk($aHitTable, 8));
print_table($aLineHitRate, $iLength);

echo "Delta:\n";
$aHitTable = array_fill(0,256,0);
$iLast = 0;
for ($i = 0; $i < $iLength; ++$i) {
    $iValue = ord($sSampleData[$i]);
    $iDelta = ($iValue - $iLast) & 0xFF;
    $iLast  = $iValue;
    $aHitTable[$iDelta]++;
}
$aLineHitRate = array_map('array_sum', array_chunk($aHitTable, 8));
print_table($aLineHitRate, $iLength);

echo "Linear-1/Delta-15:\n";
$aHitTable = array_fill(0,256,0);
$iLast = 0;
for ($i = 0; $i < $iLength; ++$i) {
    $iValue = ord($sSampleData[$i]);
    
    // 1 linear followed by 15 delta
    if ($i & 0xF) {
        $iDelta = ($iValue - $iLast) & 0xFF;
        $iLast  = $iValue;
        $aHitTable[$iDelta]++;
    } else {
        $aHitTable[$iValue]++;    
    }
}
$aLineHitRate = array_map('array_sum', array_chunk($aHitTable, 8));
print_table($aLineHitRate, $iLength);
