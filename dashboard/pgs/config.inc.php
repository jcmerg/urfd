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

// Contact email shown in the footer (obfuscated against bots via JS)
$PageOptions['ContactEmail']     = 'your_email@example.com';

// Maintenance / info banner displayed on all pages (orange bar)
// Comment out or leave empty to hide
// $PageOptions['MOTD']          = 'Scheduled maintenance on Sunday 12:00-14:00 UTC';

//############################################################################
//  Page Refresh
//############################################################################

// Auto-refresh dashboard pages via AJAX
$PageOptions['PageRefreshActive'] = true;

// Refresh interval in milliseconds (10000 = 10 seconds)
$PageOptions['PageRefreshDelay']  = '10000';

//############################################################################
//  Last Heard Page
//############################################################################

// Maximum number of stations shown in the Last Heard table
$PageOptions['LastHeardPage']['LimitTo']  = 39;

// Show callsign and module filter fields above the table
$PageOptions['UserPage']['ShowFilter']    = true;

//############################################################################
//  Repeaters / Links Page
//
//  IP privacy options (IPModus):
//    HideIP              - hide IP completely, show only masquerade chars
//    ShowLast1ByteOfIP   - show last octet only:       *.*.*123
//    ShowLast2ByteOfIP   - show last two octets:       *.*.56.123
//    ShowLast3ByteOfIP   - show last three octets:     *.34.56.123
//    ShowFullIP          - show full IP address:        12.34.56.123
//############################################################################

$PageOptions['RepeatersPage'] = array();

// Maximum number of repeaters/nodes shown
$PageOptions['RepeatersPage']['LimitTo']             = 99;

// How much of the client IP to reveal (see options above)
$PageOptions['RepeatersPage']['IPModus']             = 'ShowLast2ByteOfIP';

// Character used to mask hidden IP octets
$PageOptions['RepeatersPage']['MasqueradeCharacter'] = '*';

//############################################################################
//  Peers Page
//############################################################################

$PageOptions['PeerPage'] = array();

// Maximum number of peers shown
$PageOptions['PeerPage']['LimitTo']             = 99;

// How much of the peer IP to reveal
$PageOptions['PeerPage']['IPModus']             = 'ShowLast2ByteOfIP';

// Character used to mask hidden IP octets
$PageOptions['PeerPage']['MasqueradeCharacter'] = '*';

//############################################################################
//  SEO Meta Tags (for search engine indexing)
//############################################################################

$PageOptions['MetaDescription'] = 'URF Universal Digital Voice Reflector for Ham Radio';
$PageOptions['MetaKeywords']    = 'Ham Radio, D-Star, DMR, YSF, M17, P25, NXDN, URF, XLX';
$PageOptions['MetaAuthor']      = 'URFD Dashboard';
$PageOptions['MetaRevisit']     = 'After 30 Days';
$PageOptions['MetaRobots']      = 'index,follow';

//############################################################################
//  Service Paths (internal, usually no changes needed)
//############################################################################

$Service['PIDFile'] = '/var/run/xlxd.pid';
$Service['XMLFile'] = '/var/log/xlxd.xml';

//############################################################################
//  Admin Interface
//  Connects to the urfd admin socket for runtime management
//  Password must match the [Admin] Password in urfd.ini
//############################################################################

$Admin['Enable']   = false;
$Admin['Host']     = '127.0.0.1';
$Admin['Port']     = 10101;
$Admin['Password'] = 'changeme';

//############################################################################
//  CallingHome (XLX Directory Registration)
//
//  Registers the reflector at xlxapi.rlx.lu so it appears in Pi-Star,
//  WPSD and other hotspot software. Runs automatically every 5 minutes
//  via the supervisor callhome process in Docker deployments.
//  Set Active to false to disable registration.
//############################################################################

$CallingHome['Active']            = false;
$CallingHome['MyDashBoardURL']    = 'http://your-dashboard.example.com';
$CallingHome['ServerURL']         = 'http://xlxapi.rlx.lu/api.php';  // Do not change
$CallingHome['PushDelay']         = 10;                               // Seconds between pushes
$CallingHome['Country']           = 'Germany';
$CallingHome['Comment']           = 'URF Reflector';                  // Max 100 characters
$CallingHome['HashFile']          = '/var/lib/xlxd-ch/callinghome.php';
$CallingHome['LastCallHomefile']  = '/var/lib/xlxd-ch/lastcallhome.php';
$CallingHome['OverrideIPAddress'] = '';                                // Blank = auto-detect
$CallingHome['InterlinkFile']     = '/usr/local/etc/urfd/urfd.interlink';

//############################################################################
//  External config override
//  In Docker: mount a config.inc.php one level up to override values
//  without modifying this file. Makes git updates easier.
//############################################################################

if (file_exists("../config.inc.php")) {
  include ("../config.inc.php");
}

?>
