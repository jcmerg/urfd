<?php
/**
 * Admin API Bridge
 * Bridges HTTP POST requests to the urfd admin socket (TCP JSON protocol).
 * Handles session-based authentication with the admin socket.
 *
 * Usage:
 *   POST /api/admin.php
 *   Content-Type: application/json
 *   Body: {"action": "login", "password": "xxx"}
 *         {"action": "tg_list"}
 *         {"action": "tg_add", "protocol": "mmdvm", "tg": 26207, "module": "G", "ts": 2, "ttl": 900}
 *         {"action": "tg_remove", "protocol": "mmdvm", "tg": 26207}
 *         {"action": "status"}
 *         {"action": "reconnect", "protocol": "mmdvm"}
 */

session_start();
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['status' => 'error', 'message' => 'POST only']);
    exit;
}

// Load config
require_once(__DIR__ . '/../pgs/config.inc.php');

if (!isset($Admin) || !$Admin['Enable']) {
    http_response_code(403);
    echo json_encode(['status' => 'error', 'message' => 'Admin interface disabled']);
    exit;
}

$input = json_decode(file_get_contents('php://input'), true);
if (!$input || !isset($input['action'])) {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'Invalid request']);
    exit;
}

$action = $input['action'];

// Login action — authenticate with the admin socket and store token in session
if ($action === 'login') {
    if (!isset($input['password'])) {
        echo json_encode(['status' => 'error', 'message' => 'Missing password']);
        exit;
    }

    $result = adminSocketSend(['cmd' => 'auth', 'password' => $input['password']]);
    if ($result && isset($result['token'])) {
        $_SESSION['admin_token'] = $result['token'];
        echo json_encode(['status' => 'ok']);
    } else {
        http_response_code(401);
        echo json_encode(['status' => 'error', 'message' => $result['message'] ?? 'Authentication failed']);
    }
    exit;
}

// Logout
if ($action === 'logout') {
    unset($_SESSION['admin_token']);
    echo json_encode(['status' => 'ok']);
    exit;
}

// Check auth
if ($action === 'check_auth') {
    if (isset($_SESSION['admin_token'])) {
        echo json_encode(['status' => 'ok', 'authenticated' => true]);
    } else {
        echo json_encode(['status' => 'ok', 'authenticated' => false]);
    }
    exit;
}

// All other actions require authentication
if (!isset($_SESSION['admin_token'])) {
    http_response_code(401);
    echo json_encode(['status' => 'error', 'message' => 'Not authenticated']);
    exit;
}

$token = $_SESSION['admin_token'];

// Map actions to admin socket commands
switch ($action) {
    case 'tg_list':
        $cmd = ['cmd' => 'tg_list', 'token' => $token];
        if (isset($input['protocol'])) $cmd['protocol'] = $input['protocol'];
        break;

    case 'tg_add':
        $cmd = [
            'cmd'      => 'tg_add',
            'token'    => $token,
            'protocol' => $input['protocol'] ?? '',
            'tg'       => (int)($input['tg'] ?? 0),
            'module'   => $input['module'] ?? '',
        ];
        if (isset($input['ts']))  $cmd['ts']  = (int)$input['ts'];
        if (isset($input['ttl'])) $cmd['ttl'] = (int)$input['ttl'];
        break;

    case 'tg_remove':
        $cmd = [
            'cmd'      => 'tg_remove',
            'token'    => $token,
            'protocol' => $input['protocol'] ?? '',
            'tg'       => (int)($input['tg'] ?? 0),
        ];
        break;

    case 'status':
        $cmd = ['cmd' => 'status', 'token' => $token];
        break;

    case 'tc_stats':
        $cmd = ['cmd' => 'tc_stats', 'token' => $token];
        break;

    case 'log':
        $cmd = ['cmd' => 'log', 'token' => $token, 'lines' => (int)($input['lines'] ?? 50)];
        break;

    case 'reconnect':
        $cmd = [
            'cmd'      => 'reconnect',
            'token'    => $token,
            'protocol' => $input['protocol'] ?? '',
        ];
        break;

    default:
        echo json_encode(['status' => 'error', 'message' => 'Unknown action: ' . $action]);
        exit;
}

$result = adminSocketSend($cmd);

// If token was rejected, clear session
if ($result && isset($result['message']) && $result['message'] === 'authentication required') {
    unset($_SESSION['admin_token']);
    http_response_code(401);
}

echo json_encode($result ?: ['status' => 'error', 'message' => 'No response from admin socket']);

/**
 * Send a JSON command to the urfd admin socket and return the parsed response.
 */
function adminSocketSend(array $cmd): ?array
{
    global $Admin;

    $sock = @stream_socket_client(
        'tcp://' . $Admin['Host'] . ':' . $Admin['Port'],
        $errno, $errstr, 5
    );

    if (!$sock) {
        error_log("Admin socket connect failed: $errstr ($errno)");
        return ['status' => 'error', 'message' => 'Cannot connect to admin socket'];
    }

    stream_set_timeout($sock, 5);

    $json = json_encode($cmd) . "\n";
    fwrite($sock, $json);

    $response = fgets($sock, 8192);
    fclose($sock);

    if ($response === false) {
        return ['status' => 'error', 'message' => 'No response from admin socket'];
    }

    return json_decode(trim($response), true);
}
