<?php
// Active Users - shows connected nodes grouped by module

$Reflector->LoadFlags();

// Get module names from XML (urfd config), fallback to PHP config
$ModuleNames = array();
if ($Reflector->GetXMLContent() !== null) {
    $XML = new ParseXML();
    $xmlContent = $Reflector->GetXMLContent();
    $modulesBlock = $XML->GetElement($xmlContent, 'Modules');
    if ($modulesBlock) {
        $tmpModules = $XML->GetAllElements($modulesBlock, 'Module');
        for ($i = 0; $i < count($tmpModules); $i++) {
            $name = trim($XML->GetElement($tmpModules[$i], 'Name'));
            $desc = trim($XML->GetElement($tmpModules[$i], 'Description'));
            if ($name != '') $ModuleNames[$name] = $desc;
        }
    }
}
// Fallback to PHP config if XML has no data
if (empty($ModuleNames) && isset($PageOptions['ModuleNames'])) {
    $ModuleNames = $PageOptions['ModuleNames'];
}

// Collect modules: from XML config + from active nodes
$Modules = array_keys($ModuleNames);
$activeModules = $Reflector->GetModules();
foreach ($activeModules as $m) {
    if (!in_array($m, $Modules)) $Modules[] = $m;
}
sort($Modules, SORT_STRING);
?>

<h2 class="sub-header">Active Users</h2>
<div class="row" style="margin-top:15px;">
<?php
foreach ($Modules as $mod) {
    $name = isset($ModuleNames[$mod]) ? $ModuleNames[$mod] : '';
    $NodeIDs = $Reflector->GetNodesInModulesByID($mod);
    $count = count($NodeIDs);
?>
    <div class="col-md-4 col-sm-6" style="margin-bottom:20px;">
        <div class="panel panel-default">
            <div class="panel-heading" style="padding:12px 15px;">
                <strong style="font-size:16px;">Module <?php echo htmlspecialchars($mod); ?></strong>
<?php if (trim($name) != '') { ?>
                <span style="float:right; opacity:0.7;"><?php echo htmlspecialchars($name); ?></span>
<?php } ?>
                <div style="clear:both;"></div>
            </div>
            <div class="panel-body" style="padding:0;">
                <table class="table table-hover" style="margin-bottom:0;">
<?php
    if ($count > 0) {
        foreach ($NodeIDs as $nid) {
            $displayName = $Reflector->GetCallsignAndSuffixByID($nid);
            $qrzCall = explode('-', $displayName)[0];
            echo '<tr><td><a href="https://www.qrz.com/db/' . htmlspecialchars($qrzCall) . '" class="pl" target="_blank">' . htmlspecialchars($displayName) . '</a></td></tr>';
        }
    } else {
        echo '<tr><td class="text-muted" style="font-style:italic; text-align:center;">No nodes connected</td></tr>';
    }
?>
                </table>
            </div>
            <div class="panel-footer" style="padding:8px 15px; font-size:12px; opacity:0.7;">
                <?php echo $count; ?> node<?php echo ($count != 1) ? 's' : ''; ?> connected
            </div>
        </div>
    </div>
<?php } ?>
</div>
