<script>
var adminApiUrl = './api/admin.php';
var isAuthenticated = false;

function adminPost(data, callback) {
    $.ajax({
        url: adminApiUrl,
        type: 'POST',
        contentType: 'application/json',
        data: JSON.stringify(data),
        success: function(resp) {
            if (typeof resp === 'string') resp = JSON.parse(resp);
            callback(resp);
        },
        error: function(xhr) {
            var resp = {status: 'error', message: 'Request failed'};
            try { resp = JSON.parse(xhr.responseText); } catch(e) {}
            if (xhr.status === 401) {
                isAuthenticated = false;
                showLogin();
            }
            callback(resp);
        }
    });
}

function showLogin() {
    $('#admin-content').hide();
    $('#admin-login').show();
    $('#admin-status').html('');
}

var adminRefreshTimer = null;

function showAdmin() {
    $('#admin-login').hide();
    $('#admin-content').show();
    refreshStatus();
    refreshTGList();
    refreshTCStats();
    refreshLog();
    // Auto-refresh TG list and TC stats every 10s (without reloading the page)
    if (adminRefreshTimer) clearInterval(adminRefreshTimer);
    adminRefreshTimer = setInterval(function() {
        refreshTGList();
        refreshTCStats();
        refreshLog();
    }, 10000);
}

function doLogin() {
    var pw = $('#admin-password').val();
    adminPost({action: 'login', password: pw}, function(resp) {
        if (resp.status === 'ok') {
            isAuthenticated = true;
            showAdmin();
        } else {
            $('#login-error').text(resp.message || 'Login failed').show();
        }
    });
}

function doLogout() {
    if (adminRefreshTimer) { clearInterval(adminRefreshTimer); adminRefreshTimer = null; }
    adminPost({action: 'logout'}, function() {
        isAuthenticated = false;
        showLogin();
    });
}

function refreshTGList() {
    adminPost({action: 'tg_list'}, function(resp) {
        var tbody = $('#tg-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.mappings) {
            resp.mappings.forEach(function(m) {
                var typeLabel;
                if (m['static'] && m['primary']) typeLabel = '<span class="label label-info">statisch</span>';
                else if (m['static'] && !m['primary']) typeLabel = '<span class="label label-default">statisch (secondary)</span>';
                else if (m['primary']) typeLabel = '<span class="label label-warning">dynamisch</span>';
                else typeLabel = '<span class="label label-warning">dynamisch (secondary)</span>';

                var timeoutStr = m['static'] ? '-' :
                    '<span class="label label-default">' + formatSeconds(m.remaining) + '</span>';
                var actions = '';
                if (!m['static']) {
                    actions += '<button class="btn btn-xs btn-danger" onclick="removeTG(\'' + m.protocol + '\',' + m.tg + ')">Entfernen</button> ';
                }
                if (m.protocol === 'mmdvm' && !m['static']) {
                    actions += '<button class="btn btn-xs btn-info" onclick="doKerchunk(' + m.tg + ')" title="Kerchunk an BrandMeister senden">Kerchunk</button>';
                }
                var dirStr = m['primary'] ? 'TX/RX' : 'RX';
                tbody.append(
                    '<tr>' +
                    '<td>' + m.protocol.toUpperCase() + '</td>' +
                    '<td>' + m.tg + '</td>' +
                    '<td>' + m.module + '</td>' +
                    '<td>' + (m.ts || '-') + '</td>' +
                    '<td>' + typeLabel + '</td>' +
                    '<td>' + dirStr + '</td>' +
                    '<td>' + timeoutStr + '</td>' +
                    '<td>' + actions + '</td>' +
                    '</tr>'
                );
            });
        }
        if (resp.mappings && resp.mappings.length === 0) {
            tbody.append('<tr><td colspan="8" class="text-center">Keine TG-Mappings konfiguriert</td></tr>');
        }
    });
}

function addTG() {
    var data = {
        action: 'tg_add',
        protocol: $('#add-protocol').val(),
        tg: parseInt($('#add-tg').val()),
        module: $('#add-module').val(),
        ttl: parseInt($('#add-ttl').val()) || 900
    };
    var ts = parseInt($('#add-ts').val());
    if (ts) data.ts = ts;

    adminPost(data, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshTGList();
    });
}

function removeTG(protocol, tg) {
    adminPost({action: 'tg_remove', protocol: protocol, tg: tg}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshTGList();
    });
}

function doKerchunk(tg) {
    adminPost({action: 'kerchunk', tg: tg}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshTGList();
    });
}

function doReconnect(protocol) {
    adminPost({action: 'reconnect', protocol: protocol}, function(resp) {
        showAlert(resp);
    });
}

function doToggleBlock(action, a, b) {
    if (!a) a = $('#block-proto-a').val();
    if (!b) b = $('#block-proto-b').val();
    if (!a || !b || a === b) { alert('Zwei verschiedene Protokolle auswählen'); return; }
    adminPost({action: action, a: a, b: b}, function(resp) {
        showAlert(resp);
        refreshStatus();
    });
}

function doBlockReset() {
    adminPost({action: 'block_reset'}, function(resp) {
        showAlert(resp);
        refreshStatus();
    });
}

function refreshLog() {
    adminPost({action: 'log', lines: 50}, function(resp) {
        if (resp.status === 'ok' && resp.lines) {
            var pre = $('#log-output');
            pre.text(resp.lines.join('\n'));
            pre.scrollTop(pre[0].scrollHeight);
        }
    });
}

function refreshStatus() {
    adminPost({action: 'status'}, function(resp) {
        if (resp.status === 'ok') {
            $('#status-reflector').text(resp.reflector || '?');
            $('#status-version').text(resp.version || '?');
            // Populate module dropdown from available modules
            if (resp.modules) {
                var sel = $('#add-module');
                var current = sel.val();
                sel.empty();
                for (var i = 0; i < resp.modules.length; i++) {
                    var m = resp.modules[i];
                    sel.append('<option value="' + m + '">' + m + '</option>');
                }
                if (current) sel.val(current);
            }
            // Show/hide protocol options based on what's active
            var protoSel = $('#add-protocol');
            protoSel.empty();
            if (resp.mmdvm_active) protoSel.append('<option value="mmdvm">MMDVM</option>');
            if (resp.svx_active) protoSel.append('<option value="svx">SVX</option>');
            if (!resp.mmdvm_active && !resp.svx_active) {
                protoSel.append('<option value="">Kein Protokoll aktiv</option>');
            }
            protoSel.trigger('change');
            // Reconnect buttons
            if (resp.mmdvm_active) $('#btn-reconnect-mmdvm').show(); else $('#btn-reconnect-mmdvm').hide();
            if (resp.svx_active) $('#btn-reconnect-svx').show(); else $('#btn-reconnect-svx').hide();

            // Block rules — collect bidirectional pairs
            var blockedPairs = {};  // "A|B" (sorted) -> true
            if (resp.blocks && resp.blocks.length > 0) {
                resp.blocks.forEach(function(b) {
                    if (b.from === b.to) return;
                    var key = [b.from, b.to].sort().join('|');
                    blockedPairs[key] = true;
                });
                var html = '';
                for (var k in blockedPairs) {
                    var parts = k.split('|');
                    html += '<span class="label label-danger" style="margin-right:4px;cursor:pointer;" '
                        + 'onclick="doToggleBlock(\'unblock\',\'' + parts[0] + '\',\'' + parts[1] + '\')" '
                        + 'title="Klick zum Aufheben">'
                        + parts[0] + ' &#8596; ' + parts[1] + ' &#10005;</span> ';
                }
                $('#block-rules').html(html);
            } else {
                $('#block-rules').html('<span class="label label-success">Keine Blockierungen</span>');
            }

            // Populate protocol dropdowns from active protocols
            if (resp.active_protocols) {
                var selA = $('#block-proto-a'), selB = $('#block-proto-b');
                var curA = selA.val(), curB = selB.val();
                selA.empty(); selB.empty();
                resp.active_protocols.sort().forEach(function(p) {
                    selA.append('<option value="' + p + '">' + p + '</option>');
                    selB.append('<option value="' + p + '">' + p + '</option>');
                });
                if (curA) selA.val(curA);
                if (curB) selB.val(curB);
            }
        }
    });
}

function refreshTCStats() {
    adminPost({action: 'tc_stats'}, function(resp) {
        var tbody = $('#tc-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.modules) {
            var codecNames = {0:'-', 1:'D-Star', 2:'DMR', 3:'Codec2/1600', 4:'Codec2/3200', 5:'P25', 6:'USRP/PCM', 7:'SVX/OPUS'};
            resp.modules.forEach(function(m) {
                var connLabel = m.connected ?
                    '<span class="label label-success">verbunden</span>' :
                    '<span class="label label-danger">getrennt</span>';
                var streamLabel = m.streaming ?
                    '<span class="label label-info">' + (m.user || '?') + '</span>' :
                    '<span class="label label-default">idle</span>';
                var codec = codecNames[m.codec_in] || '-';
                var rtt = '-';
                var packets = m.total_packets || 0;
                var mismatches = m.mismatch_count || 0;
                if (!m.connected) {
                    codec = '-';
                    packets = '-';
                    mismatches = '-';
                } else {
                    if (m.rt_count > 0 && m.rt_min_ms >= 0) {
                        rtt = m.rt_min_ms + ' / ' + m.rt_avg_ms + ' / ' + m.rt_max_ms + ' ms';
                    }
                }
                tbody.append(
                    '<tr>' +
                    '<td>' + m.module + '</td>' +
                    '<td>' + connLabel + '</td>' +
                    '<td>' + streamLabel + '</td>' +
                    '<td>' + codec + '</td>' +
                    '<td>' + packets + '</td>' +
                    '<td>' + mismatches + '</td>' +
                    '<td>' + rtt + '</td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.modules || resp.modules.length === 0) {
            tbody.append('<tr><td colspan="7" class="text-center">Keine transcodierten Module</td></tr>');
        }
    });
}

function showAlert(resp) {
    var cls = resp.status === 'ok' ? 'alert-success' : 'alert-danger';
    var msg = resp.message || resp.status;
    $('#admin-alerts').html('<div class="alert ' + cls + ' alert-dismissible">' +
        '<button type="button" class="close" data-dismiss="alert">&times;</button>' +
        msg + '</div>');
}

function formatSeconds(s) {
    if (s < 0) return '-';
    var m = Math.floor(s / 60);
    var sec = s % 60;
    return m + 'min ' + sec + 's';
}

$(document).ready(function() {
    // Suspend the dashboard auto-refresh to prevent clearing input fields
    if (typeof SuspendPageRefresh === 'function') SuspendPageRefresh();

    adminPost({action: 'check_auth'}, function(resp) {
        if (resp.authenticated) {
            isAuthenticated = true;
            showAdmin();
            refreshStatus();
        } else {
            showLogin();
        }
    });

    $('#admin-password').keypress(function(e) {
        if (e.which === 13) doLogin();
    });

    // Show/hide timeslot based on protocol selection
    $(document).on('change', '#add-protocol', function() {
        if ($(this).val() === 'mmdvm') {
            $('#ts-group').show();
        } else {
            $('#ts-group').hide();
        }
    });
});
</script>

<style>
#admin-login { max-width: 400px; margin: 80px auto; }
.admin-section { margin-bottom: 30px; }
.admin-section h4 { border-bottom: 1px solid #555; padding-bottom: 8px; margin-bottom: 15px; }
.add-tg-form .form-group { margin-right: 8px; margin-bottom: 8px; }
.add-tg-form label { font-size: 11px; display: block; margin-bottom: 2px; color: #999; }
</style>

<!-- Login Form -->
<div id="admin-login" style="display:none;">
    <div class="panel panel-default">
        <div class="panel-heading"><h3 class="panel-title">Admin Login</h3></div>
        <div class="panel-body">
            <div id="login-error" class="alert alert-danger" style="display:none;"></div>
            <div class="form-group">
                <label for="admin-password">Password</label>
                <input type="password" class="form-control" id="admin-password" placeholder="Admin Password">
            </div>
            <button class="btn btn-primary" onclick="doLogin()">Login</button>
        </div>
    </div>
</div>

<!-- Admin Content -->
<div id="admin-content" style="display:none;">
    <div id="admin-alerts"></div>

    <!-- TG Management -->
    <div class="admin-section">
        <h4>Talkgroup Management</h4>

        <!-- Add TG Form -->
        <div class="well">
            <form class="form-inline add-tg-form" onsubmit="addTG(); return false;">
                <div class="form-group">
                    <label>Protokoll</label>
                    <select class="form-control" id="add-protocol">
                        <option value="mmdvm">MMDVM</option>
                        <option value="svx">SVX</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Talkgroup</label>
                    <input type="number" class="form-control" id="add-tg" placeholder="z.B. 26207" style="width:120px;" required>
                </div>
                <div class="form-group">
                    <label>Modul</label>
                    <select class="form-control" id="add-module" style="width:80px;" required>
                        <option value="">-</option>
                    </select>
                </div>
                <div class="form-group" id="ts-group">
                    <label>Timeslot</label>
                    <select class="form-control" id="add-ts">
                        <option value="2">TS2</option>
                        <option value="1">TS1</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Timeout (Sek.)</label>
                    <input type="number" class="form-control" id="add-ttl" value="900" style="width:100px;">
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Aktivieren</button>
                </div>
            </form>
        </div>

        <!-- TG Table -->
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Protokoll</th>
                    <th>Talkgroup</th>
                    <th>Modul</th>
                    <th>Timeslot</th>
                    <th>Typ</th>
                    <th>Richtung</th>
                    <th>Timeout</th>
                    <th>Aktionen</th>
                </tr>
            </thead>
            <tbody id="tg-table-body">
                <tr><td colspan="8" class="text-center">Laden...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshTGList()">Aktualisieren</button>
    </div>

    <!-- Transcoder Statistics -->
    <div class="admin-section">
        <h4>Transcoder</h4>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Modul</th>
                    <th>Verbindung</th>
                    <th>Stream</th>
                    <th>Codec</th>
                    <th>Pakete</th>
                    <th>Mismatches</th>
                    <th>RTT (min/avg/max)</th>
                </tr>
            </thead>
            <tbody id="tc-table-body">
                <tr><td colspan="8" class="text-center">Laden...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshTCStats()">Aktualisieren</button>
    </div>

    <!-- Protocol Controls -->
    <div class="admin-section">
        <h4>Protokoll-Steuerung</h4>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-mmdvm" onclick="doReconnect('mmdvm')">MMDVM Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-svx" onclick="doReconnect('svx')">SVX Reconnect</button>
        <div style="margin-top:10px;" id="block-rules"></div>
        <div style="margin-top:10px;">
            <select id="block-proto-a" class="form-control input-sm" style="width:140px;display:inline-block;"></select>
            <span> &#8596; </span>
            <select id="block-proto-b" class="form-control input-sm" style="width:140px;display:inline-block;"></select>
            <button class="btn btn-danger btn-sm" onclick="doToggleBlock('block')">Block</button>
            <button class="btn btn-success btn-sm" onclick="doToggleBlock('unblock')">Unblock</button>
            <button class="btn btn-default btn-sm" onclick="doBlockReset()">Reset auf Config-Default</button>
        </div>
    </div>

    <!-- Status -->
    <div class="admin-section">
        <h4>Reflector Status</h4>
        <table class="table table-condensed" style="max-width:400px;">
            <tr><td>Reflector</td><td id="status-reflector">-</td></tr>
            <tr><td>Version</td><td id="status-version">-</td></tr>
        </table>
    </div>

    <!-- Live Log -->
    <div class="admin-section">
        <h4>Log</h4>
        <pre id="log-output" style="max-height:300px;overflow-y:auto;font-size:11px;background:#1a1a1a;color:#ccc;padding:8px;border:1px solid #333;"></pre>
        <button class="btn btn-default btn-sm" onclick="refreshLog()">Aktualisieren</button>
    </div>

    <hr>
    <button class="btn btn-default btn-sm" onclick="doLogout()">Logout</button>
</div>
