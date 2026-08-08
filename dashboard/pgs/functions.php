<?php

function GetSystemUptime() {
   $out = exec("uptime");
   return substr($out, 0, strpos($out, ","));
}

function Debug($message) {
   echo '<br><hr><pre>';
   print_r($message);
   echo '</pre><hr><br>';
}

function ParseTime($Input) {
   
    if (strpos($Input, "<") !== false) {
       $Input = substr($Input, 0, strpos($Input, "<"));
    }
    
    if (strpos($Input, "T") !== false) {
        return strtotime($Input);
    }

    // Tuesday Tue Nov 17 14:23:22 2015
    $tmp  = explode(" ", $Input);
    if (strlen(trim($tmp[3])) == 0) {
       unset($tmp[3]);
       $tmp = array_values($tmp);
    }

    $tmp1 = explode(":", $tmp[4]); 
    $month = "";
    switch (strtolower($tmp[2])) {
      case 'jan' : $month = 1; break;
      case 'feb' : $month = 2; break;
      case 'mar' : $month = 3; break;
      case 'apr' : $month = 4; break;
      case 'may' : $month = 5; break;
      case 'jun' : $month = 6; break;
      case 'jul' : $month = 7; break;
      case 'aug' : $month = 8; break;
      case 'sep' : $month = 9; break;
      case 'oct' : $month = 10; break;
      case 'nov' : $month = 11; break;
      case 'dec' : $month = 12; break;
      default    : $month = 1; 
    }
    return @mktime($tmp1[0], $tmp1[1], $tmp1[2], $month, $tmp[3], $tmp[5]);
    
}

function FormatSeconds($seconds) {
  $seconds = abs($seconds); 
  return sprintf("%d days %02d:%02d:%02d", $seconds/60/60/24,($seconds/60/60)%24,($seconds/60)%60,$seconds%60);
} 

// Fetch the XLX directory reflector list. Returns an array of <reflector> elements,
// or false if the directory server could not be reached. The explicit timeout matters:
// without it PHP waits default_socket_timeout on an unreachable host and ties up an
// fpm worker for the whole page load.
function FetchReflectorList($ServerURL, $Timeout = 5) {
	$ctx = @stream_context_create(array('http' => array('method'        => 'GET',
	                                                    'timeout'       => $Timeout,
	                                                    'ignore_errors' => true)));
	$Input = @file_get_contents($ServerURL."?do=GetReflectorList", false, $ctx);
	if ($Input === false) {
		error_log("Reflector list: no response from ".$ServerURL);
		return false;
	}

	$XML           = new ParseXML();
	$Reflectorlist = $XML->GetElement($Input, "reflectorlist");
	$Reflectors    = $XML->GetAllElements($Reflectorlist, "reflector");

	// A reachable-but-broken server (error page, maintenance notice) yields no
	// elements; treat that like an outage rather than letting count() fatal.
	if (!is_array($Reflectors)) {
		error_log("Reflector list: unparseable response from ".$ServerURL);
		return false;
	}
	return $Reflectors;
}

// Inline notice for pages whose remote data source is unavailable.
function DirectoryUnavailableNotice() {
	return '<div class="alert alert-warning">The XLX directory server is currently '
	     . 'unreachable, so this list cannot be shown. The reflector itself is '
	     . 'unaffected and keeps running normally.</div>';
}

function CreateCode ($laenge) {
	$zeichen = "1234567890abcdefghijklmnopqrstuvwyxzABCDEFGHIJKLMNAOPQRSTUVWYXZ";   
	mt_srand( (double) microtime() * 1000000); 
	$out = "";
	for ($i=1;$i<=$laenge;$i++){ 
		$out .= $zeichen[mt_rand(0,(strlen($zeichen)-1))];       
	}         
	return $out;  
}

?>