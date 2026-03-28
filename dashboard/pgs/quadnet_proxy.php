<?php
header('Content-Type: text/plain; charset=utf-8');
header('Cache-Control: no-cache');

$url = 'https://irc-1.openquad.net/ics/ics';
$ctx = stream_context_create([
    'http' => [
        'method'  => 'GET',
        'header'  => "Range: bytes=-10240\r\n",
        'timeout' => 5
    ]
]);

$data = @file_get_contents($url, false, $ctx);
if ($data === false) {
    http_response_code(502);
    echo 'Error fetching QuadNet data';
    exit;
}

// Strip potential leading partial line
$nl = strpos($data, "\n");
if ($nl !== false && $nl > 0) {
    $data = substr($data, $nl + 1);
}

echo $data;
?>
