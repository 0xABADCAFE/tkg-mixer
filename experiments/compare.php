<?php

declare(strict_types=1);


$iScaleFactor = 61; 

$aScaleTable = [];

for ($i = -256; $i < 256; ++$i) {
    $aScaleTable[$i] = $i * $iScaleFactor;
}


//print_r($aScaleTable);

$sSampleData = file_get_contents("linear.raw");
$iLength = strlen($sSampleData);

$iLastSample8 = 0;

$iLastSample16A = 0;
$iLastSample16B = 0;

$iIntegratedA = 0;
$iIntegratedB = 0;

$iMinDelta8 = 1000;
$iMaxDelta8 = -1000;

for ($i = 0; $i < $iLength; ++$i) {
    $iSampleU8    = ord($sSampleData[$i]);
    $iSample8     = $iSampleU8 > 127 ? ($iSampleU8 - 256) : $iSampleU8;
    $iDelta8      = $iSample8 - $iLastSample8;
    $iLastSample8 = $iSample8;
    
    $iSample16 = $aScaleTable[$iSample8];
    
    $iDelta16A = $iSample16 - $iLastSample16A;
    $iLastSample16A = $iSample16;
 
    $iDelta16B = $aScaleTable[$iDelta8];
    
    $iIntegratedA += $iDelta16A;
    $iIntegratedB += $iDelta16B;
    
    if ($iSample16 !== $iIntegratedA || $iSample16 !== $iIntegratedB) {
        printf(
            "%6d: S8: %3d S16: %5d D8: %3d D16A: %5d D16B: %5d I16A: %5d I16B: %5d\n",
            $i,
            $iSample8,
            $iSample16,
            $iDelta8,
            $iDelta16A,
            $iDelta16B,
            $iIntegratedA,
            $iIntegratedB
        );
        break;
    }
    $iMinDelta8 = min($iDelta8, $iMinDelta8);
    $iMaxDelta8 = max($iDelta8, $iMaxDelta8);
}

printf("Min/Max delta-8 %d/%d\n", $iMinDelta8, $iMaxDelta8);
