<?php


$sLeftChan = file_get_contents('../lchan_out.raw');
$sLeftVol  = file_get_contents('../lvol_out.raw');

$sRightChan = file_get_contents('../rchan_out.raw');
$sRightVol  = file_get_contents('../rvol_out.raw');

$iLimit = strlen($sLeftChan);

$sOutput = '';

$iLeftFactor = 0;
$iRightFactor = 0;

for ($i = 0; $i < $iLimit; ++ $i) {

	if (!($i & 0xF)) {
		// Volume data is actually 16-bit words
		$iLeftFactor  = 4 * ord($sLeftVol[1 + ($i >> 3)]);
		$iRightFactor = 4 * ord($sRightVol[1 + ($i >> 3)]);
	}
	
	$iLeftSample = ord($sLeftChan[$i]);
	if ($iLeftSample > 127) {
		$iLeftSample -= 256;
	}
	$iLeftSample  *= $iLeftFactor;

	$iRightSample = ord($sRightChan[$i]);
	if ($iRightSample > 127) {
		$iRightSample -= 256;
	}

	$iRightSample *= $iRightFactor;
	
	$sOutput .= pack('vv', $iLeftSample & 0xFFFF, $iRightSample & 0xFFFF);
}

file_put_contents('out_companded_16le.raw', $sOutput);
