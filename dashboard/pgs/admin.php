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
    refreshDcsMapList();
    refreshYsfMapList();
    refreshSvxsUserList();
    refreshMmdvmUserList();
    refreshTCStats();
    refreshLog();
    // Auto-refresh every 10s
    if (adminRefreshTimer) clearInterval(adminRefreshTimer);
    adminRefreshTimer = setInterval(function() {
        refreshTGList();
        refreshDcsMapList();
        refreshYsfMapList();
        refreshSvxsUserList();
        refreshMmdvmUserList();
        refreshMmdvmPeerList();
        refreshSvxsPeerList();
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
                if (m['static'] && m['primary']) typeLabel = '<span class="label label-info">static</span>';
                else if (m['static'] && !m['primary']) typeLabel = '<span class="label label-default">static (secondary)</span>';
                else if (m['primary']) typeLabel = '<span class="label label-warning">dynamic</span>';
                else typeLabel = '<span class="label label-warning">dynamic (secondary)</span>';

                var timeoutStr = m['static'] ? '-' :
                    '<span class="label label-default">' + formatSeconds(m.remaining) + '</span>';
                var actions = '';
                if (!m['static']) {
                    actions += '<button class="btn btn-xs btn-danger" onclick="removeTG(\'' + m.protocol + '\',' + m.tg + ')">Remove</button> ';
                }
                if (m.protocol === 'mmdvmclient' && !m['static']) {
                    actions += '<button class="btn btn-xs btn-info" onclick="doKerchunk(' + m.tg + ')" title="Send kerchunk to BrandMeister">Kerchunk</button>';
                }
                var dirStr = m['primary'] ? 'TX/RX' : 'RX';
                tbody.append(
                    '<tr>' +
                    '<td>' + ({mmdvmclient:'MMDVMClient',svxreflector:'SvxReflector',svx:'SVX'}[m.protocol] || m.protocol) + '</td>' +
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
            tbody.append('<tr><td colspan="8" class="text-center">No TG mappings configured</td></tr>');
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

function refreshDcsMapList() {
    // aggregate all D-Star client mappings (DCS, DExtra, DPlus)
    var tbody = $('#dcs-map-table-body');
    tbody.empty();
    var allMappings = [];
    var pending = 3;
    function renderAll() {
        if (--pending > 0) return;
        allMappings.forEach(function(m) {
            var connLabel = m.connected ?
                '<span class="label label-success">connected</span>' :
                '<span class="label label-danger">disconnected</span>';
            tbody.append(
                '<tr>' +
                '<td>' + m.proto + '</td>' +
                '<td>' + m.host + ':' + m.port + '</td>' +
                '<td>' + m.remote_module + '</td>' +
                '<td>' + m.local_module + '</td>' +
                '<td>' + connLabel + '</td>' +
                '<td><button class="btn btn-xs btn-danger" onclick="removeDstarMap(\'' + m.proto_key + '\',\'' + m.local_module + '\')">Remove</button></td>' +
                '</tr>'
            );
        });
        if (allMappings.length === 0) {
            tbody.append('<tr><td colspan="6" class="text-center">No D-Star client mappings configured</td></tr>');
        }
    }
    $.each([
        {action: 'dcs_map_list', proto: 'DCS', key: 'dcs'},
        {action: 'dextra_map_list', proto: 'DExtra', key: 'dextra'},
        {action: 'dplus_map_list', proto: 'DPlus', key: 'dplus'}
    ], function(_, p) {
        adminPost({action: p.action}, function(resp) {
            if (resp.status === 'ok' && resp.mappings) {
                resp.mappings.forEach(function(m) {
                    m.proto = p.proto; m.proto_key = p.key;
                    allMappings.push(m);
                });
            }
            renderAll();
        });
    });
}

function addDcsMap() {
    var proto = $('#dcs-proto').val();
    var portDefaults = {dcs: 30051, dextra: 30001, dplus: 20001};
    var data = {
        action: proto + '_map_add',
        host: $('#dcs-host').val(),
        port: parseInt($('#dcs-port').val()) || portDefaults[proto] || 30051,
        remote_module: $('#dcs-remote-mod').val(),
        local_module: $('#dcs-local-mod').val()
    };
    adminPost(data, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshDcsMapList();
    });
}

function removeDstarMap(protoKey, localMod) {
    adminPost({action: protoKey + '_map_remove', local_module: localMod}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshDcsMapList();
    });
}

function refreshYsfMapList() {
    adminPost({action: 'ysf_map_list'}, function(resp) {
        var tbody = $('#ysf-map-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.mappings) {
            resp.mappings.forEach(function(m) {
                var connLabel = m.connected ?
                    '<span class="label label-success">connected</span>' :
                    '<span class="label label-danger">disconnected</span>';
                var dgidLabel = m.dgid > 0 ? m.dgid : '-';
                tbody.append(
                    '<tr>' +
                    '<td>' + m.host + ':' + m.port + '</td>' +
                    '<td>' + m.local_module + '</td>' +
                    '<td>' + dgidLabel + '</td>' +
                    '<td>' + connLabel + '</td>' +
                    '<td><button class="btn btn-xs btn-danger" onclick="removeYsfMap(\'' + m.local_module + '\')">Remove</button></td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.mappings || resp.mappings.length === 0) {
            tbody.append('<tr><td colspan="5" class="text-center">No YSF mappings configured</td></tr>');
        }
    });
}

function addYsfMap() {
    var data = {
        action: 'ysf_map_add',
        host: $('#ysf-host').val(),
        port: parseInt($('#ysf-port').val()) || 42000,
        local_module: $('#ysf-local-mod').val(),
        dgid: parseInt($('#ysf-dgid').val()) || 0
    };
    adminPost(data, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshYsfMapList();
    });
}

function removeYsfMap(localMod) {
    adminPost({action: 'ysf_map_remove', local_module: localMod}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshYsfMapList();
    });
}

function refreshSvxsUserList() {
    adminPost({action: 'svxs_user_list'}, function(resp) {
        var tbody = $('#svxs-user-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.users) {
            resp.users.forEach(function(u) {
                tbody.append(
                    '<tr>' +
                    '<td>' + u + '</td>' +
                    '<td><button class="btn btn-xs btn-danger" onclick="removeSvxsUser(\'' + u + '\')">Remove</button></td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.users || resp.users.length === 0) {
            tbody.append('<tr><td colspan="2" class="text-center">No SVX users configured</td></tr>');
        }
    });
}

function addSvxsUser() {
    var data = {
        action: 'svxs_user_add',
        callsign: $('#svxs-callsign').val(),
        password: $('#svxs-password').val()
    };
    adminPost(data, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') {
            $('#svxs-callsign').val('');
            $('#svxs-password').val('');
            refreshSvxsUserList();
        }
    });
}

function removeSvxsUser(callsign) {
    adminPost({action: 'svxs_user_remove', callsign: callsign}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshSvxsUserList();
    });
}

function refreshMmdvmUserList() {
    adminPost({action: 'mmdvm_user_list'}, function(resp) {
        var tbody = $('#mmdvm-user-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.users) {
            resp.users.forEach(function(u) {
                tbody.append(
                    '<tr>' +
                    '<td>' + u.dmrid + '</td>' +
                    '<td>' + (u.callsign || '') + '</td>' +
                    '<td><button class="btn btn-xs btn-danger" onclick="removeMmdvmUser(' + u.dmrid + ')">Remove</button></td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.users || resp.users.length === 0) {
            tbody.append('<tr><td colspan="3" class="text-center">No MMDVM users configured</td></tr>');
        }
    });
}

function addMmdvmUser() {
    var id = $('#mmdvm-user-id').val().trim();
    var pw = $('#mmdvm-user-pw').val().trim();
    if (!id || !pw) return;
    var data = {action: 'mmdvm_user_add', password: pw};
    if (/^\d+$/.test(id))
        data.dmrid = parseInt(id);
    else
        data.callsign = id;
    adminPost(data, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') {
            $('#mmdvm-user-id').val('');
            $('#mmdvm-user-pw').val('');
            refreshMmdvmUserList();
        }
    });
}

function removeMmdvmUser(dmrid) {
    adminPost({action: 'mmdvm_user_remove', dmrid: dmrid}, function(resp) {
        showAlert(resp);
        if (resp.status === 'ok') refreshMmdvmUserList();
    });
}

function refreshMmdvmPeerList() {
    adminPost({action: 'mmdvm_peer_list'}, function(resp) {
        var tbody = $('#mmdvm-peers-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.peers) {
            resp.peers.forEach(function(p) {
                var loc = (p.peer_info && p.peer_info.location) ? p.peer_info.location : '';
                var sw = (p.peer_info && p.peer_info.softwareId) ? p.peer_info.softwareId : '';
                tbody.append(
                    '<tr>' +
                    '<td>' + (p.callsign || '') + '</td>' +
                    '<td>' + (p.dmrid || '') + '</td>' +
                    '<td>' + loc + '</td>' +
                    '<td>' + sw + '</td>' +
                    '<td>' + (p.module || '') + '</td>' +
                    '<td>' + (p.connected_since || '') + '</td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.peers || resp.peers.length === 0) {
            tbody.append('<tr><td colspan="6" class="text-center">No MMDVM nodes connected</td></tr>');
        }
    });
}

function refreshSvxsPeerList() {
    adminPost({action: 'svxs_peer_list'}, function(resp) {
        var tbody = $('#svxs-peers-table-body');
        tbody.empty();
        if (resp.status === 'ok' && resp.peers) {
            resp.peers.forEach(function(p) {
                var tgs = p.subscribed_tgs ? p.subscribed_tgs.join(', ') : '';
                var sw = (p.node_info && p.node_info.software) ? p.node_info.software : '';
                var udp = p.udp_discovered ? 'Yes' : 'No';
                tbody.append(
                    '<tr>' +
                    '<td>' + (p.callsign || '') + '</td>' +
                    '<td>' + tgs + '</td>' +
                    '<td>' + sw + '</td>' +
                    '<td>' + udp + '</td>' +
                    '<td>' + (p.connected_since || '') + '</td>' +
                    '</tr>'
                );
            });
        }
        if (!resp.peers || resp.peers.length === 0) {
            tbody.append('<tr><td colspan="5" class="text-center">No SVX nodes connected</td></tr>');
        }
    });
}

function doToggleBlock(action, a, b) {
    if (!a) a = $('#block-proto-a').val();
    if (!b) b = $('#block-proto-b').val();
    if (!a || !b || a === b) { alert('Select two different protocols'); return; }
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

function doClearUsers() {
    adminPost({action: 'clear_users'}, function(resp) {
        showAlert(resp);
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

                // also populate DCS and YSF local module dropdowns
                $.each(['#dcs-local-mod', '#ysf-local-mod'], function(_, sel) {
                    var $s = $(sel), cur = $s.val();
                    $s.empty();
                    for (var i = 0; i < resp.modules.length; i++)
                        $s.append('<option value="' + resp.modules[i] + '">' + resp.modules[i] + '</option>');
                    if (cur) $s.val(cur);
                });
            }
            // Show/hide protocol options based on what's active
            var protoSel = $('#add-protocol');
            protoSel.empty();
            if (resp.mmdvm_active) protoSel.append('<option value="mmdvmclient">MMDVMClient</option>');
            if (resp.svx_active) protoSel.append('<option value="svxreflector">SvxReflector</option>');
            if (resp.svxs_active) protoSel.append('<option value="svx">SVX</option>');
            if (!resp.mmdvm_active && !resp.svx_active && !resp.svxs_active) {
                protoSel.append('<option value="">No TG protocol active</option>');
            }
            protoSel.trigger('change');
            // Reconnect buttons
            if (resp.mmdvm_active) $('#btn-reconnect-mmdvm').show(); else $('#btn-reconnect-mmdvm').hide();
            if (resp.svx_active) $('#btn-reconnect-svx').show(); else $('#btn-reconnect-svx').hide();
            if (resp.dcsclient_active) $('#btn-reconnect-dcsclient').show(); else $('#btn-reconnect-dcsclient').hide();
            if (resp.dextraclient_active) $('#btn-reconnect-dextraclient').show(); else $('#btn-reconnect-dextraclient').hide();
            if (resp.dplusclient_active) $('#btn-reconnect-dplusclient').show(); else $('#btn-reconnect-dplusclient').hide();
            if (resp.dcsclient_active || resp.dextraclient_active || resp.dplusclient_active) $('#dcs-mapping-section').show(); else $('#dcs-mapping-section').hide();
            if (resp.ysfclient_active) { $('#btn-reconnect-ysfclient').show(); $('#ysf-mapping-section').show(); } else { $('#btn-reconnect-ysfclient').hide(); $('#ysf-mapping-section').hide(); }
            if (resp.svxs_active) { $('#svxs-user-section').show(); $('#svxs-peers-section').show(); refreshSvxsPeerList(); } else { $('#svxs-user-section').hide(); $('#svxs-peers-section').hide(); }
            if (resp.mmdvm_server_active) { $('#mmdvm-user-section').show(); $('#mmdvm-peers-section').show(); refreshMmdvmUserList(); refreshMmdvmPeerList(); } else { $('#mmdvm-user-section').hide(); $('#mmdvm-peers-section').hide(); }

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
                        + 'title="Click to unblock">'
                        + parts[0] + ' &#8596; ' + parts[1] + ' &#10005;</span> ';
                }
                $('#block-rules').html(html);
            } else {
                $('#block-rules').html('<span class="label label-success">No active blocks</span>');
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
                    '<span class="label label-success">connected</span>' :
                    '<span class="label label-danger">disconnected</span>';
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
            tbody.append('<tr><td colspan="7" class="text-center">No transcoded modules</td></tr>');
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
        if ($(this).val() === 'mmdvmclient') {
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
                    <label>Protocol</label>
                    <select class="form-control" id="add-protocol">
                        <option value="mmdvmclient">MMDVMClient</option>
                        <option value="svxreflector">SvxReflector</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Talkgroup</label>
                    <input type="number" class="form-control" id="add-tg" placeholder="e.g. 26207" style="width:120px;" required>
                </div>
                <div class="form-group">
                    <label>Module</label>
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
                    <label>Timeout (sec)</label>
                    <input type="number" class="form-control" id="add-ttl" value="900" style="width:100px;">
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Add</button>
                </div>
            </form>
        </div>

        <!-- TG Table -->
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Protocol</th>
                    <th>Talkgroup</th>
                    <th>Module</th>
                    <th>Timeslot</th>
                    <th>Type</th>
                    <th>Direction</th>
                    <th>Timeout</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="tg-table-body">
                <tr><td colspan="8" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshTGList()">Refresh</button>
    </div>

    <!-- D-Star Client Mappings (DCS, DExtra, DPlus) -->
    <div class="admin-section" id="dcs-mapping-section" style="display:none;">
        <h4>D-Star Client Mappings</h4>
        <div class="well">
            <form class="form-inline add-tg-form" onsubmit="addDcsMap(); return false;">
                <div class="form-group">
                    <label>Protocol</label>
                    <select class="form-control" id="dcs-proto" style="width:100px;" onchange="var p={dcs:30051,dextra:30001,dplus:20001};$('#dcs-port').val(p[this.value]||30051);">
                        <option value="dcs">DCS</option>
                        <option value="dextra">DExtra</option>
                        <option value="dplus">DPlus</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Host</label>
                    <input type="text" class="form-control" id="dcs-host" placeholder="e.g. dcs001.xreflector.net" style="width:220px;" required>
                </div>
                <div class="form-group">
                    <label>Port</label>
                    <input type="number" class="form-control" id="dcs-port" value="30051" style="width:90px;">
                </div>
                <div class="form-group">
                    <label>Remote Module</label>
                    <select class="form-control" id="dcs-remote-mod" style="width:70px;">
                        <script>for(var i=65;i<=90;i++) document.write('<option value="'+String.fromCharCode(i)+'">'+String.fromCharCode(i)+'</option>');</script>
                    </select>
                </div>
                <div class="form-group">
                    <label>Local Module</label>
                    <select class="form-control" id="dcs-local-mod" style="width:70px;">
                        <option value="">-</option>
                    </select>
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Connect</button>
                </div>
            </form>
        </div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Protocol</th>
                    <th>Reflector</th>
                    <th>Remote Module</th>
                    <th>Local Module</th>
                    <th>Status</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="dcs-map-table-body">
                <tr><td colspan="6" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshDcsMapList()">Refresh</button>
    </div>

    <!-- YSF Client Mapping -->
    <div class="admin-section" id="ysf-mapping-section" style="display:none;">
        <h4>YSF Client Mappings</h4>
        <div class="well">
            <form class="form-inline add-tg-form" onsubmit="addYsfMap(); return false;">
                <div class="form-group">
                    <label>Host</label>
                    <input type="text" class="form-control" id="ysf-host" placeholder="e.g. ysf.reflector.net" style="width:220px;" required>
                </div>
                <div class="form-group">
                    <label>Port</label>
                    <input type="number" class="form-control" id="ysf-port" value="42000" style="width:90px;">
                </div>
                <div class="form-group">
                    <label>Local Module</label>
                    <select class="form-control" id="ysf-local-mod" style="width:70px;">
                        <option value="">-</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>DG-ID</label>
                    <input type="number" class="form-control" id="ysf-dgid" value="0" min="0" max="99" style="width:70px;">
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Connect</button>
                </div>
            </form>
        </div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Reflector</th>
                    <th>Local Module</th>
                    <th>DG-ID</th>
                    <th>Status</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="ysf-map-table-body">
                <tr><td colspan="4" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshYsfMapList()">Refresh</button>
    </div>

    <!-- SVX Server User Management -->
    <div class="admin-section" id="svxs-user-section" style="display:none;">
        <h4>SVX Server Users</h4>
        <div class="well">
            <form class="form-inline add-tg-form" onsubmit="addSvxsUser(); return false;">
                <div class="form-group">
                    <label>Callsign</label>
                    <input type="text" class="form-control" id="svxs-callsign" placeholder="e.g. DL4JC" style="width:130px;" required>
                </div>
                <div class="form-group">
                    <label>Password</label>
                    <input type="text" class="form-control" id="svxs-password" placeholder="Password" style="width:160px;" required>
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Add</button>
                </div>
            </form>
        </div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Callsign</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="svxs-user-table-body">
                <tr><td colspan="2" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshSvxsUserList()">Refresh</button>
    </div>

    <!-- Connected SVX Nodes -->
    <div class="admin-section" id="svxs-peers-section" style="display:none;">
        <h4>Connected SVX Nodes</h4>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Callsign</th>
                    <th>TGs</th>
                    <th>Software</th>
                    <th>UDP</th>
                    <th>Connected Since</th>
                </tr>
            </thead>
            <tbody id="svxs-peers-table-body">
                <tr><td colspan="5" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshSvxsPeerList()">Refresh</button>
    </div>

    <!-- MMDVM Server User Management -->
    <div class="admin-section" id="mmdvm-user-section" style="display:none;">
        <h4>MMDVM Server Users</h4>
        <div class="well">
            <form class="form-inline add-tg-form" onsubmit="addMmdvmUser(); return false;">
                <div class="form-group">
                    <label>Callsign or DMR ID</label>
                    <input type="text" class="form-control" id="mmdvm-user-id" placeholder="e.g. DL4JC or 2634000" style="width:180px;" required>
                </div>
                <div class="form-group">
                    <label>Password</label>
                    <input type="text" class="form-control" id="mmdvm-user-pw" placeholder="Password" style="width:160px;" required>
                </div>
                <div class="form-group" style="vertical-align:bottom;">
                    <label>&nbsp;</label>
                    <button type="submit" class="btn btn-success" style="display:block;">Add</button>
                </div>
            </form>
        </div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>DMR ID</th>
                    <th>Callsign</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="mmdvm-user-table-body">
                <tr><td colspan="3" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshMmdvmUserList()">Refresh</button>
    </div>

    <!-- Connected MMDVM Nodes -->
    <div class="admin-section" id="mmdvm-peers-section" style="display:none;">
        <h4>Connected MMDVM Nodes</h4>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Callsign</th>
                    <th>DMR ID</th>
                    <th>Location</th>
                    <th>Software</th>
                    <th>Module</th>
                    <th>Connected Since</th>
                </tr>
            </thead>
            <tbody id="mmdvm-peers-table-body">
                <tr><td colspan="6" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshMmdvmPeerList()">Refresh</button>
    </div>

    <!-- Transcoder Statistics -->
    <div class="admin-section">
        <h4>Transcoder</h4>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>Module</th>
                    <th>Connection</th>
                    <th>Stream</th>
                    <th>Codec</th>
                    <th>Packets</th>
                    <th>Mismatches</th>
                    <th>RTT (min/avg/max)</th>
                </tr>
            </thead>
            <tbody id="tc-table-body">
                <tr><td colspan="8" class="text-center">Loading...</td></tr>
            </tbody>
        </table>
        <button class="btn btn-default btn-sm" onclick="refreshTCStats()">Refresh</button>
    </div>

    <!-- Protocol Controls -->
    <div class="admin-section">
        <h4>Protocol Controls</h4>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-mmdvm" onclick="doReconnect('mmdvmclient')">MMDVMClient Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-svx" onclick="doReconnect('svxreflector')">SvxReflector Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-dcsclient" onclick="doReconnect('dcsclient')" style="display:none;">DCS Client Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-dextraclient" onclick="doReconnect('dextraclient')" style="display:none;">DExtra Client Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-dplusclient" onclick="doReconnect('dplusclient')" style="display:none;">DPlus Client Reconnect</button>
        <button class="btn btn-warning btn-sm" id="btn-reconnect-ysfclient" onclick="doReconnect('ysfclient')" style="display:none;">YSF Client Reconnect</button>
        <div style="margin-top:10px;" id="block-rules"></div>
        <div style="margin-top:10px;">
            <select id="block-proto-a" class="form-control input-sm" style="width:140px;display:inline-block;"></select>
            <span> &#8596; </span>
            <select id="block-proto-b" class="form-control input-sm" style="width:140px;display:inline-block;"></select>
            <button class="btn btn-danger btn-sm" onclick="doToggleBlock('block')">Block</button>
            <button class="btn btn-success btn-sm" onclick="doToggleBlock('unblock')">Unblock</button>
            <button class="btn btn-default btn-sm" onclick="doBlockReset()">Reset to Config Default</button>
        </div>
    </div>

    <!-- Status -->
    <div class="admin-section">
        <h4>Reflector Status</h4>
        <table class="table table-condensed" style="max-width:400px;">
            <tr><td>Reflector</td><td id="status-reflector">-</td></tr>
            <tr><td>Version</td><td id="status-version">-</td></tr>
        </table>
        <button class="btn btn-warning btn-sm" onclick="doClearUsers()">Clear Last Heard</button>
    </div>

    <!-- Live Log -->
    <div class="admin-section">
        <h4>Log</h4>
        <pre id="log-output" style="max-height:300px;overflow-y:auto;font-size:11px;background:#1a1a1a;color:#ccc;padding:8px;border:1px solid #333;"></pre>
        <button class="btn btn-default btn-sm" onclick="refreshLog()">Refresh</button>
    </div>

    <hr>
    <button class="btn btn-default btn-sm" onclick="doLogout()">Logout</button>
</div>
