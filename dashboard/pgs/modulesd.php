<?php
// Overview Modules & Protocols - reads extended XML data from urfd daemon

$xmlModules = array();
$xmlProtocols = array();
$xmlReflector = array();

if ($Reflector->GetXMLContent() !== null) {
    $XML = new ParseXML();
    $xmlContent = $Reflector->GetXMLContent();

    // Parse <Reflector> metadata
    $refBlock = $XML->GetElement($xmlContent, 'Reflector');
    if ($refBlock) {
        $xmlReflector['Callsign'] = $XML->GetElement($refBlock, 'Callsign');
        $xmlReflector['Country'] = $XML->GetElement($refBlock, 'Country');
    }

    // Parse <Modules> section with mappings
    $modulesBlock = $XML->GetElement($xmlContent, 'Modules');
    if ($modulesBlock) {
        $tmpModules = $XML->GetAllElements($modulesBlock, 'Module');
        for ($i = 0; $i < count($tmpModules); $i++) {
            $name = trim($XML->GetElement($tmpModules[$i], 'Name'));
            if ($name == '') continue;

            // Parse all <Mapping> entries for this module
            $mappings = array();
            $tmpMappings = $XML->GetAllElements($tmpModules[$i], 'Mapping');
            for ($j = 0; $j < count($tmpMappings); $j++) {
                $mappings[] = array(
                    'protocol'   => $XML->GetElement($tmpMappings[$j], 'Protocol'),
                    'type'       => $XML->GetElement($tmpMappings[$j], 'Type'),
                    'id'         => $XML->GetElement($tmpMappings[$j], 'ID'),
                    'timeslot'   => $XML->GetElement($tmpMappings[$j], 'Timeslot'),
                    'remoteName' => $XML->GetElement($tmpMappings[$j], 'RemoteName'),
                );
            }

            $xmlModules[$name] = array(
                'description' => $XML->GetElement($tmpModules[$i], 'Description'),
                'linkedNodes' => (int)$XML->GetElement($tmpModules[$i], 'LinkedNodes'),
                'transcoded'  => ($XML->GetElement($tmpModules[$i], 'Transcoded') === 'true'),
                'mappings'    => $mappings,
                'dmrplus'     => $XML->GetElement($tmpModules[$i], 'DMRplus'),
                'ysfdgid'     => $XML->GetElement($tmpModules[$i], 'YSFDGID'),
            );
        }
    }

    // Parse <Protocols> section
    $protocolsBlock = $XML->GetElement($xmlContent, 'Protocols');
    if ($protocolsBlock) {
        $tmpProtocols = $XML->GetAllElements($protocolsBlock, 'Protocol');
        for ($i = 0; $i < count($tmpProtocols); $i++) {
            $pName = trim($XML->GetElement($tmpProtocols[$i], 'Name'));
            $pPort = trim($XML->GetElement($tmpProtocols[$i], 'Port'));
            if ($pName != '') {
                $xmlProtocols[] = array('name' => $pName, 'port' => $pPort);
            }
        }
    }
}

// Build module list - prefer XML data, fallback to config
$Modules = array();
foreach ($xmlModules as $mod => $data) {
    $configName = isset($PageOptions['ModuleNames'][$mod]) ? $PageOptions['ModuleNames'][$mod] : '';
    $Modules[$mod] = array(
        'name'        => ($data['description'] != '') ? $data['description'] : $configName,
        'linkedNodes' => $data['linkedNodes'],
        'transcoded'  => $data['transcoded'],
        'mappings'    => $data['mappings'],
        'dmrplus'     => $data['dmrplus'],
        'ysfdgid'     => $data['ysfdgid'],
    );
}

// Also include config-only modules not in XML
if (isset($PageOptions['ModuleNames'])) {
    foreach ($PageOptions['ModuleNames'] as $mod => $name) {
        if (!isset($Modules[$mod]) && trim($name) != '') {
            $Modules[$mod] = array('name' => $name, 'linkedNodes' => 0, 'transcoded' => false, 'mappings' => array(), 'dmrplus' => '', 'ysfdgid' => '');
        }
    }
}

ksort($Modules);

// Count live connected nodes per module
for ($i = 0; $i < $Reflector->NodeCount(); $i++) {
    $mod = $Reflector->Nodes[$i]->GetLinkedModule();
    if (isset($Modules[$mod])) {
        if (!isset($Modules[$mod]['liveNodes'])) $Modules[$mod]['liveNodes'] = array();
        $Modules[$mod]['liveNodes'][] = $Reflector->Nodes[$i];
    }
}

// Count heard stations per module
$stationCounts = array();
for ($i = 0; $i < $Reflector->StationCount(); $i++) {
    $mod = $Reflector->Stations[$i]->GetModule();
    if (!isset($stationCounts[$mod])) $stationCounts[$mod] = 0;
    $stationCounts[$mod]++;
}
?>

<h2 class="sub-header">Overview Modules</h2>
<div class="table-responsive">
    <table class="table table-striped table-hover">
        <tr class="table-center">
            <th>Module</th>
            <th>Name</th>
            <th>DMR+</th>
            <th>YSF DG-ID</th>
            <th>Nodes</th>
            <th>Transcoded</th>
            <th>Mappings</th>
            <th>Connected Nodes</th>
        </tr>
<?php
foreach ($Modules as $mod => $info) {
    $liveNodes = isset($info['liveNodes']) ? $info['liveNodes'] : array();
    $nodeCount = count($liveNodes);
    $heardCount = isset($stationCounts[$mod]) ? $stationCounts[$mod] : 0;

    // Connected nodes list - collapsible if many
    $nodeList = array();
    foreach ($liveNodes as $node) {
        $cs = trim($node->GetCallSign());
        $proto = $node->GetProtocol();
        $nodeList[] = htmlspecialchars($cs) . ' <span class="text-muted">(' . htmlspecialchars($proto) . ')</span>';
    }
    if ($nodeCount == 0) {
        $nodeListStr = '<span class="text-muted">&mdash;</span>';
    } else if ($nodeCount <= 3) {
        $nodeListStr = implode('<br/>', $nodeList);
    } else {
        // Show first 2 + expandable rest
        $collapseId = 'nodes_' . $mod;
        $nodeListStr = implode('<br/>', array_slice($nodeList, 0, 2))
            . '<br/><a data-toggle="collapse" href="#' . $collapseId . '" style="font-size:11px;">'
            . '+ ' . ($nodeCount - 2) . ' more</a>'
            . '<div class="collapse" id="' . $collapseId . '">'
            . implode('<br/>', array_slice($nodeList, 2))
            . '</div>';
    }

    // Transcoded badge
    $tcBadge = $info['transcoded']
        ? '<span class="label label-success" style="background-color:var(--success,#66bb6a);">Yes</span>'
        : '<span class="label label-default">No</span>';

    // Mappings display - unified style
    $mappingParts = array();
    foreach ($info['mappings'] as $map) {
        $proto = htmlspecialchars($map['protocol']);
        $type  = htmlspecialchars($map['type']);
        $id    = htmlspecialchars($map['id']);
        $ts    = htmlspecialchars($map['timeslot']);
        $rname = htmlspecialchars($map['remoteName']);

        $label = $proto;
        $details = array();
        if ($type == 'TG')       $details[] = 'TG' . $id;
        if ($type == 'AutoLink') $details[] = 'AutoLink';
        if ($type == 'Bridge')   $details[] = 'Bridge';
        if ($id != '' && $type != 'TG') $details[] = '#' . $id;
        if ($ts != '')           $details[] = $ts;
        if ($rname != '')        $details[] = $rname;
        if (count($details) > 0) $label .= ': ' . implode(' ', $details);

        $mappingParts[] = '<span class="label label-info" style="background-color:#1976d2;margin:2px;display:inline-block;padding:4px 8px;font-size:12px;font-weight:normal;border-radius:4px;">'
            . $label . '</span>';
    }
    $mappingsStr = count($mappingParts) > 0 ? implode(' ', $mappingParts) : '<span class="text-muted">&mdash;</span>';

    $dmrpStr = ($info['dmrplus'] != '') ? htmlspecialchars($info['dmrplus']) : '<span class="text-muted">&mdash;</span>';
    $ysfStr  = ($info['ysfdgid'] != '') ? htmlspecialchars($info['ysfdgid']) : '<span class="text-muted">&mdash;</span>';

    echo '<tr class="table-center">';
    echo '<td><strong>' . htmlspecialchars($mod) . '</strong></td>';
    echo '<td>' . htmlspecialchars($info['name']) . '</td>';
    echo '<td>' . $dmrpStr . '</td>';
    echo '<td>' . $ysfStr . '</td>';
    echo '<td>' . $nodeCount . '</td>';
    echo '<td>' . $tcBadge . '</td>';
    echo '<td style="text-align:left;">' . $mappingsStr . '</td>';
    echo '<td style="text-align:left;">' . $nodeListStr . '</td>';
    echo '</tr>';
}

if (count($Modules) == 0) {
    echo '<tr><td colspan="8" class="text-muted">No modules configured</td></tr>';
}
?>
    </table>
</div>

<?php if (count($xmlProtocols) > 0) { ?>
<h2 class="sub-header" style="margin-top:30px;">Enabled Protocols</h2>
<div class="table-responsive">
    <table class="table table-striped table-hover">
        <tr class="table-center">
            <th>Protocol</th>
            <th>Port</th>
            <th>Type</th>
        </tr>
<?php
    $protoTypes = array(
        'DExtra' => 'D-Star', 'DPlus' => 'D-Star', 'DCS' => 'D-Star',
        'MMDVM' => 'DMR', 'DMRPlus' => 'DMR', 'XLXPeer' => 'XLX/BM Peering', 'BMMmdvm' => 'DMR',
        'M17' => 'M17', 'YSF' => 'YSF/C4FM', 'P25' => 'P25', 'NXDN' => 'NXDN',
        'URF' => 'URF Interlink', 'G3' => 'D-Star (G3)', 'USRP' => 'AllStar/USRP',
    );

    foreach ($xmlProtocols as $proto) {
        $type = isset($protoTypes[$proto['name']]) ? $protoTypes[$proto['name']] : 'Other';
        $portDisplay = ($proto['port'] == '0') ? '<span class="text-muted">dynamic</span>' : htmlspecialchars($proto['port']);
        echo '<tr class="table-center">';
        echo '<td><strong>' . htmlspecialchars($proto['name']) . '</strong></td>';
        echo '<td>' . $portDisplay . '</td>';
        echo '<td>' . htmlspecialchars($type) . '</td>';
        echo '</tr>';
    }
?>
    </table>
</div>
<?php } ?>
