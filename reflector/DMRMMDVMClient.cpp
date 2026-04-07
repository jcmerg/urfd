//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
// Copyright © 2021 Thomas A. Early N7TAE
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <fstream>
#include "DMRMMDVMClient.h"


////////////////////////////////////////////////////////////////////////////////////////
// constructors

CDmrmmdvmClient::CDmrmmdvmClient()
{
	m_SlotModule[0] = m_SlotModule[1] = ' ';
}

CDmrmmdvmClient::CDmrmmdvmClient(const CCallsign &callsign, const CIp &ip, char reflectorModule)
	: CClient(callsign, ip, reflectorModule)
{
	m_SlotModule[0] = m_SlotModule[1] = ' ';
}

CDmrmmdvmClient::CDmrmmdvmClient(const CDmrmmdvmClient &client)
	: CClient(client)
{
	m_SlotModule[0] = client.m_SlotModule[0];
	m_SlotModule[1] = client.m_SlotModule[1];
}

////////////////////////////////////////////////////////////////////////////////////////
// status

bool CDmrmmdvmClient::IsAlive(void) const
{
	return (m_LastKeepaliveTime.time() < DMRMMDVM_KEEPALIVE_TIMEOUT);
}

////////////////////////////////////////////////////////////////////////////////////////
// reporting

void CDmrmmdvmClient::WriteXml(std::ofstream &xmlFile)
{
	xmlFile << "<NODE>" << std::endl;
	xmlFile << "\t<Callsign>" << m_Callsign << "</Callsign>" << std::endl;
	xmlFile << "\t<IP>" << m_Ip.GetAddress() << "</IP>" << std::endl;
	xmlFile << "\t<LinkedModule>" << GetReflectorModule() << "</LinkedModule>" << std::endl;
	xmlFile << "\t<LinkedModuleTS1>" << m_SlotModule[0] << "</LinkedModuleTS1>" << std::endl;
	xmlFile << "\t<LinkedModuleTS2>" << m_SlotModule[1] << "</LinkedModuleTS2>" << std::endl;
	xmlFile << "\t<Protocol>" << GetProtocolName() << "</Protocol>" << std::endl;
	char mbstr[100];
	if (std::strftime(mbstr, sizeof(mbstr), "%FT%TZ", std::gmtime(&m_ConnectTime)))
	{
		xmlFile << "\t<ConnectTime>" << mbstr << "</ConnectTime>" << std::endl;
	}
	if (std::strftime(mbstr, sizeof(mbstr), "%FT%TZ", std::gmtime(&m_LastHeardTime)))
	{
		xmlFile << "\t<LastHeardTime>" << mbstr << "</LastHeardTime>" << std::endl;
	}
	xmlFile << "</NODE>" << std::endl;
}
