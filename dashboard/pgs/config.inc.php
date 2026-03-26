<?php
//############################################################################
//  URFD Dashboard Configuration
//############################################################################

$Service     = array();
$CallingHome = array();
$PageOptions = array();

//############################################################################
//  Dashboard Settings
//############################################################################

$PageOptions['DashboardVersion'] = '2.6.0';

$PageOptions['ContactEmail']     = 'your_email@example.com';

// Maintenance banner (comment out or leave empty to hide)
// $PageOptions['MOTD']          = 'Scheduled maintenance on Sunday 12:00-14:00 UTC';

//############################################################################
//  Page Refresh
//############################################################################

$PageOptions['PageRefreshActive'] = true;
$PageOptions['PageRefreshDelay']  = '10000';  // milliseconds

//############################################################################
//  Last Heard Page
//############################################################################

$PageOptions['LastHeardPage']['LimitTo']  = 39;
$PageOptions['UserPage']['ShowFilter']    = true;

//############################################################################
//  Repeaters / Links Page
//  IPModus: HideIP, ShowFullIP, ShowLast1ByteOfIP, ShowLast2ByteOfIP, ShowLast3ByteOfIP
//############################################################################

$PageOptions['RepeatersPage'] = array();
$PageOptions['RepeatersPage']['LimitTo']             = 99;
$PageOptions['RepeatersPage']['IPModus']             = 'ShowLast2ByteOfIP';
$PageOptions['RepeatersPage']['MasqueradeCharacter'] = '*';

//############################################################################
//  Peers Page
//############################################################################

$PageOptions['PeerPage'] = array();
$PageOptions['PeerPage']['LimitTo']             = 99;
$PageOptions['PeerPage']['IPModus']             = 'ShowLast2ByteOfIP';
$PageOptions['PeerPage']['MasqueradeCharacter'] = '*';

//############################################################################
//  SEO Meta Tags
//############################################################################

$PageOptions['MetaDescription'] = 'URF Universal Digital Voice Reflector for Ham Radio';
$PageOptions['MetaKeywords']    = 'Ham Radio, D-Star, DMR, YSF, M17, P25, NXDN, URF, XLX';
$PageOptions['MetaAuthor']      = 'URFD Dashboard';
$PageOptions['MetaRevisit']     = 'After 30 Days';
$PageOptions['MetaRobots']      = 'index,follow';

//############################################################################
//  Service Paths
//############################################################################

$Service['PIDFile'] = '/var/run/xlxd.pid';
$Service['XMLFile'] = '/var/log/xlxd.xml';

//############################################################################
//  CallingHome (XLX Directory Registration)
//  Registers reflector at xlxapi.rlx.lu. Runs automatically via supervisor.
//  Set Active to false to disable.
//############################################################################

$CallingHome['Active']            = false;
$CallingHome['MyDashBoardURL']    = 'http://your-dashboard.example.com';
$CallingHome['ServerURL']         = 'http://xlxapi.rlx.lu/api.php';  // Do not change
$CallingHome['PushDelay']         = 10;                               // seconds
$CallingHome['Country']           = 'Germany';
$CallingHome['Comment']           = 'URF Reflector';                  // max 100 chars
$CallingHome['HashFile']          = '/var/lib/xlxd-ch/callinghome.php';
$CallingHome['LastCallHomefile']  = '/var/lib/xlxd-ch/lastcallhome.php';
$CallingHome['OverrideIPAddress'] = '';                                // blank = auto-detect
$CallingHome['InterlinkFile']     = '/usr/local/etc/urfd/urfd.interlink';

//############################################################################
//  External config override (mounted volume in Docker)
//  Allows local overrides without modifying this file.
//############################################################################

if (file_exists("../config.inc.php")) {
  include ("../config.inc.php");
}

?>
