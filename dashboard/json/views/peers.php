<?php

/*** Add peers to payload ***/
// Initialise so an empty reflector encodes as [] rather than null.
$payload = array();

for ($i=0;$i<$Reflector->PeerCount();$i++) {

    $payload[$i] = array(
        'callsign'      => $Reflector->Peers[$i]->GetCallSign(),
        'ip'            => $Reflector->Peers[$i]->GetIP(),
        'linkedmodule'  => $Reflector->Peers[$i]->GetLinkedModule(),
        'connecttime'   => gmdate('Y-m-d\TH:i:sp', $Reflector->Peers[$i]->GetConnectTime()),
        'lastheardtime' => gmdate('Y-m-d\TH:i:sp', $Reflector->Peers[$i]->GetLastHeardTime())
    );

}


// json encode payload array
$records = json_encode($payload);

echo $records;

?>