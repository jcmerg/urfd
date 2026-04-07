/*
 *   Copyright (c) 2023 by Thomas A. Early N7TAE
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <string>

#pragma once

// configuration key names
struct SJsonKeys {
	struct DCS { const std::string port, enable, peercallsign; }
	dcs { "DCSPort", "DCSEnable", "DCSPeerCallsign" };

	struct PORTENABLE { const std::string port, enable; }
	dextra { "DExtraPort", "DExtraEnable" },
	dmrplus { "DMRPlusPort", "DMRPlusEnable" },
	dplus { "DPlusPort", "DPlusEnable" },
	m17 { "M17Port", "M17Enable" },
	urf { "URFPort", "URFEnable" };

	struct G3 { const std::string enable; }
	g3 { "G3Enable" };

	struct XLXPEER { const std::string enable, port, peercallsign; }
	xlxpeer { "bmEnable", "bmPort", "bmPeerCallsign" };

	struct MMDVM { const std::string port, fallbackdmrid, enable, requireauth; }
	mmdvm { "MMDVMPort", "mmdvmFallbackDmrId", "MMDVMEnable", "mmdvmRequireAuth" };

	struct NAMES { const std::string callsign, bootstrap, url, email, country, sponsor; }
	names { "Callsign", "bootstrap", "DashboardUrl", "SysopEmail", "Country", "Sponsor" };

	struct IP { const std::string ipv4bind, ipv4address, ipv6bind, ipv6address; }
	ip { "ipv4bind", "IPv4Address", "ipv6bind", "IPv6Address" };

	struct TC { const std::string port, bind, modules; }
	tc { "tcport", "tcbind", "TranscodedModules" };

	struct MODULES { const std::string modules, descriptor[26]; }
	modules { "Modules",
		"DescriptionA", "DescriptionB", "DescriptionC", "DescriptionD", "DescriptionE", "DescriptionF", "DescriptionG", "DescriptionH", "DescriptionI", "DescriptionJ", "DescriptionK", "DescriptionL", "DescriptionM", "DescriptionN", "DescriptionO", "DescriptionP", "DescriptionQ", "DescriptionR", "DescriptionS", "DescriptionT", "DescriptionU", "DescriptionV", "DescriptionW", "DescriptionX", "DescriptionY", "DescriptionZ" };

	struct USRP { const std::string enable, ip, txport, rxport, module, callsign, filepath; }
	usrp { "usrpEnable", "usrpIpAddress", "urspTxPort", "usrpRxPort", "usrpModule", "usrpCallsign", "usrpFilePath" };

	struct P25NXDN { const std::string port, autolinkmod, reflectorid, enable; }
	p25 { "P25Port",  "P25AutolinkMod",   "P25ReflectorID",  "P25Enable" };

	struct NXDN { const std::string port, autolinkmod, reflectorid, enable, fallbacknxdnid; }
	nxdn { "NXDNPort", "NXDNAutolinkMod", "NXDNReflectorID", "NXDNEnable", "NXDNFallbackNxdnId" };

	struct YSF { const std::string port, autolinkmod, enabledgid, defaulttxfreq, defaultrxfreq, enable;
		struct YSLREG { const std::string id, name, description; } ysfreflectordb; }
	ysf { "YSFPort", "YSFAutoLinkMod", "YSFEnableDGID", "YSFDefaultTxFreq", "YSFDefaultRxFreq", "YSFEnable",
		{ "ysfrefdbid", "ysfrefdbname", "ysfrefdbdesc" } };

	struct DB { const std::string url, mode, refreshmin, filepath; }
	dmriddb   {  "dmrIdDbUrl",  "dmrIdDbMode",  "dmrIdDbRefresh",  "dmrIdDbFilePath" },
	nxdniddb  { "nxdnIdDbUrl", "nxdnIdDbMode", "nxdnIdDbRefresh", "nxdnIdDbFilePath" },
	ysftxrxdb {  "ysfIdDbUrl",  "ysfIdDbMode",  "ysfIdDbRefresh",  "ysfIdDbFilePath" };

	struct ECHO { const std::string enable, module; }
	echo { "EchoEnable", "EchoModule" };

	struct FILES { const std::string pid, xml, json, white, black, interlink, terminal; }
	files { "pidFilePath", "xmlFilePath", "jsonFilePath", "whitelistFilePath", "blacklistFilePath", "interlinkFilePath", "g3TerminalFilePath" };
	struct MMDVMCLIENT { const std::string enable, address, port, localport, dmrid, password, callsign, latitude, longitude, location, description, url, rxfreq, txfreq, software, firmware, fallbackdmrid, blockprotocols, bmapikey; }
	mmdvmclient { "mmdvmcliEnable", "mmdvmcliAddress", "mmdvmcliPort", "mmdvmcliLocalPort", "mmdvmcliDmrId", "mmdvmcliPassword", "mmdvmcliCallsign", "mmdvmcliLatitude", "mmdvmcliLongitude", "mmdvmcliLocation", "mmdvmcliDescription", "mmdvmcliUrl", "mmdvmcliRxFreq", "mmdvmcliTxFreq", "mmdvmcliSoftware", "mmdvmcliFirmware", "mmdvmcliFallbackDmrId", "mmdvmcliBlockProtocols", "mmdvmcliBmApiKey" };
	struct SVX { const std::string enable, host, port, callsign, password, blockprotocols, rxgain, txgain; }
	svx { "svxEnable", "svxHost", "svxPort", "svxCallsign", "svxPassword", "svxBlockProtocols", "svxRxGain", "svxTxGain" };
	struct SVXS { const std::string enable, port, blockprotocols, rxgain, txgain; }
	svxs { "svxsEnable", "svxsPort", "svxsBlockProtocols", "svxsRxGain", "svxsTxGain" };
	struct DCSCLIENT { const std::string enable, callsign, blockprotocols; }
	dcsclient { "dcsClientEnable", "dcsClientCallsign", "dcsClientBlockProtocols" };
	struct YSFCLIENT { const std::string enable, callsign, blockprotocols; }
	ysfclient { "ysfClientEnable", "ysfClientCallsign", "ysfClientBlockProtocols" };
	struct DEXTRACLIENT { const std::string enable, callsign, blockprotocols; }
	dextraclient { "dextraClientEnable", "dextraClientCallsign", "dextraClientBlockProtocols" };
	struct DPLUSCLIENT { const std::string enable, callsign, blockprotocols; }
	dplusclient { "dplusClientEnable", "dplusClientCallsign", "dplusClientBlockProtocols" };
	struct ADMIN { const std::string enable, port, password, bindaddress; }
	admin { "adminEnable", "adminPort", "adminPassword", "adminBindAddress" };
};
