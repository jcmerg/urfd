// urfd -- The universal reflector
// Copyright © 2024 Thomas A. Early N7TAE
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

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/tcp.h>

#include "TCSocket.h"

void CTCSocket::Close()
{
	for (auto &item : m_Pfd)
	{
		if (item.fd >= 0)
		{
			Close(item.fd);
		}
	}
	m_Pfd.clear();
}

void CTCSocket::Close(char mod)
{
	auto pos = m_Modules.find(mod);
	if (std::string::npos == pos)
	{
		std::cerr << "Could not find module '" << mod << "'" << std::endl;
		return;
	}
	if (m_Pfd[pos].fd < 0)
	{
		std::cerr << "Close(" << mod << ") is already closed" << std::endl;
		return;
	}
	Close(m_Pfd[pos].fd);
	m_Pfd[pos].fd = -1;
}

void CTCSocket::Close(int fd)
{
	if (fd < 0)
		return;

	for (auto &p : m_Pfd)
	{
		if (fd == p.fd)
		{
			shutdown(p.fd, SHUT_RDWR); // may fail if already disconnected, that's OK
			close(p.fd);
			p.fd = -1;
			return;
		}
	}
}

bool CTCSocket::IsModuleConnected(char module)
{
	auto pos = m_Modules.find(module);
	if (std::string::npos == pos || m_Pfd[pos].fd < 0)
		return false;

	// Quick non-blocking poll to detect dead sockets (POLLHUP/POLLERR from keepalive)
	struct pollfd pfd = { m_Pfd[pos].fd, POLLIN, 0 };
	int rv = poll(&pfd, 1, 0);
	if (rv > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
	{
		std::cerr << "Transcoder module '" << module << "' connection lost" << std::endl;
		Close(m_Pfd[pos].fd);
		return false;
	}
	return true;
}

int CTCSocket::GetFD(char module) const
{
	auto pos = m_Modules.find(module);
	if (std::string::npos == pos)
		return -1;
	return m_Pfd[pos].fd;
}

char CTCSocket::GetMod(int fd) const
{
	for (unsigned i=0; i<m_Pfd.size(); i++)
	{
		if (fd == m_Pfd[i].fd)
		{
			return m_Modules[i];
		}
	}
	return '?';
}

bool CTCServer::AnyAreClosed()
{
	for (unsigned i = 0; i < m_Pfd.size(); i++)
	{
		if (m_Pfd[i].fd < 0)
			return true;

		// Actively probe for dead connections (e.g. half-open after network outage)
		struct pollfd pfd = { m_Pfd[i].fd, POLLIN, 0 };
		int rv = poll(&pfd, 1, 0);
		if (rv > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
		{
			std::cerr << "Transcoder module '" << m_Modules[i] << "' connection lost (maintenance probe)" << std::endl;
			Close(m_Pfd[i].fd);
			return true;
		}
	}
	return false;
}

bool CTCSocket::Send(const STCPacket *packet)
{
	auto pos = m_Modules.find(packet->module);
	if (pos == std::string::npos)
	{
		if(packet->codec_in == ECodecType::ping)
		{
			pos = 0; // There is at least one transcoding module, use it to send the ping
		}
		else
		{
			std::cerr << "Can't Send() this packet to unconfigured module '" << packet->module << "'" << std::endl;
			return true;
		}
	}
	unsigned count = 0;
	auto data = (const unsigned char *)packet;
	do {
		auto n = send(m_Pfd[pos].fd, data+count, sizeof(STCPacket)-count, 0);
		if (n <= 0)
		{
			if (0 == n)
			{
				std::cerr << "CTCSocket::Send: socket on module '" << packet->module << "' has been closed!" << std::endl;
			}
			else
			{
				perror("CTCSocket::Send");
			}
			Close(packet->module);
			return true;
		}
		count += n;
	} while (count < sizeof(STCPacket));
	return false;
}

bool CTCSocket::receive(int fd, STCPacket *packet)
{
	auto n = recv(fd, packet, sizeof(STCPacket), MSG_WAITALL);
	if (n < 0)
	{
		return true;
	}

	if (0 == n)
	{
		return true;
	}

	if (n != sizeof(STCPacket))
		std::cout << "receive() only read " << n << " bytes of the transcoder packet from module '" << GetMod(fd) << "'" << std::endl;
	return false;
}

// returns true if there is data to return
bool CTCServer::Receive(char module, STCPacket *packet, int ms)
{
	bool rv = false;
	const auto pos = m_Modules.find(module);
	if (pos == std::string::npos)
	{
		std::cerr << "Can't receive on unconfigured module '" << module << "'" << std::endl;
		return rv;
	}

	auto pfds = &m_Pfd[pos];
	if (pfds->fd < 0)
	{
		return rv;
	}

	auto n = poll(pfds, 1, ms);
	if (n < 0)
	{
		perror("Recieve poll");
		Close(pfds->fd);
		return rv;
	}

	if (0 == n)
		return rv;	// timeout

	if (pfds->revents & POLLIN)
	{
		rv = receive(pfds->fd, packet);
	}

	// It's possible that even if we read the data, the socket can have an error after the read...
	// So we'll check...
	if (pfds->revents & POLLERR || pfds->revents & POLLHUP)
	{
		if (pfds->revents & POLLERR)
			std::cerr << "POLLERR received on module '" << module << "', closing socket" << std::endl;
		if (pfds->revents & POLLHUP)
			std::cerr << "POLLHUP received on module '" << module << "', closing socket" << std::endl;
		Close(pfds->fd);
	}
	if (pfds->revents & POLLNVAL)
	{
		std::cerr << "POLLNVAL received on module " << module << "'" << std::endl;
	}

	if (rv)
		Close(pfds->fd);
		
	if(packet->codec_in == ECodecType::ping)
		return false; 
	else 
		return !rv;
}

bool CTCServer::ReceiveNoPoll(char module, STCPacket *packet)
{
	const auto pos = m_Modules.find(module);
	if (pos == std::string::npos || m_Pfd[pos].fd < 0)
		return false;

	int fd = m_Pfd[pos].fd;
	if (receive(fd, packet))
	{
		Close(fd);
		return false;
	}
	if (packet->codec_in == ECodecType::ping)
		return false;
	return true;
}

bool CTCServer::Open(const std::string &address, const std::string &modules, uint16_t port)
{
	m_Modules.assign(modules);

	m_Ip = CIp(address.c_str(), AF_UNSPEC, SOCK_STREAM, port);

	m_Pfd.resize(m_Modules.size());
	for (auto &pf : m_Pfd)
	{
		pf.fd = -1;
		pf.events = POLLIN;
		pf.revents = 0;
	}

	// Create persistent listen socket
	m_ListenFd = socket(m_Ip.GetFamily(), SOCK_STREAM, 0);
	if (m_ListenFd < 0)
	{
		perror("Open socket");
		return true;
	}

	int yes = 1;
	if (setsockopt(m_ListenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
	{
		close(m_ListenFd);
		m_ListenFd = -1;
		perror("Open setsockopt");
		return true;
	}

	if (bind(m_ListenFd, m_Ip.GetCPointer(), m_Ip.GetSize()) < 0)
	{
		close(m_ListenFd);
		m_ListenFd = -1;
		perror("Open bind");
		return true;
	}

	if (listen(m_ListenFd, 3) < 0)
	{
		perror("Open listen");
		close(m_ListenFd);
		m_ListenFd = -1;
		return true;
	}

	std::cout << "Waiting at " << m_Ip << " for transcoder connections for modules " << m_Modules << "..." << std::endl;

	// Non-blocking: don't wait for TCD here, maintenance thread handles it
	return false;
}

void CTCServer::TryAccept(int ms)
{
	if (m_ListenFd < 0)
		return;

	// Poll listen socket for incoming connections
	struct pollfd pfd = { m_ListenFd, POLLIN, 0 };
	int rv = poll(&pfd, 1, ms);
	if (rv <= 0)
		return;

	if (!(pfd.revents & POLLIN))
		return;

	// Accept all pending connections (there may be multiple modules connecting)
	while (acceptone())
		;
}

// Returns true if there are more connections to accept, false if done or error
bool CTCServer::acceptone()
{
	// Non-blocking check if another connection is ready
	struct pollfd pfd = { m_ListenFd, POLLIN, 0 };
	int rv = poll(&pfd, 1, 0);
	if (rv <= 0 || !(pfd.revents & POLLIN))
		return false;

	CIp their_addr;
	socklen_t sin_size = sizeof(struct sockaddr_storage);

	auto newfd = accept(m_ListenFd, their_addr.GetPointer(), &sin_size);
	if (newfd < 0)
	{
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			perror("Accept accept");
		return false;
	}

	// Set a short timeout on the ID byte recv so we don't block forever
	struct timeval tv = { 2, 0 };
	setsockopt(newfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	char mod;
	rv = recv(newfd, &mod, 1, MSG_WAITALL);
	if (rv != 1)
	{
		if (rv < 0)
			perror("Accept recv");
		else
			std::cerr << "recv got no identification byte!" << std::endl;
		close(newfd);
		return true; // try next pending connection
	}

	const auto pos = m_Modules.find(mod);
	if (std::string::npos == pos)
	{
		std::cerr << "New connection for module '" << mod << "', but it's not configured!" << std::endl;
		close(newfd);
		return true;
	}

	// Close old stale connection if present
	if (m_Pfd[pos].fd >= 0)
	{
		std::cerr << "Replacing stale connection on module '" << mod << "'" << std::endl;
		shutdown(m_Pfd[pos].fd, SHUT_RDWR);
		close(m_Pfd[pos].fd);
	}

	// Clear the recv timeout for normal operation
	struct timeval notv = { 0, 0 };
	setsockopt(newfd, SOL_SOCKET, SO_RCVTIMEO, &notv, sizeof(notv));

	// Enable TCP keepalive to detect dead connections
	int keepalive = 1;
	setsockopt(newfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
	int idle = 10;      // start probes after 10s idle
	int interval = 5;   // probe every 5s
	int count = 3;      // 3 failed probes = dead
	setsockopt(newfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	setsockopt(newfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
	setsockopt(newfd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

	{ std::ostringstream s; s << "File descriptor " << newfd << " opened TCP port for module '" << mod << "' on " << their_addr; std::cout << s.str() << std::endl; }

	m_Pfd[pos].fd = newfd;

	return true; // check for more
}

bool CTCClient::Open(const std::string &address, const std::string &modules, uint16_t port)
{
	m_Address.assign(address);
	m_Modules.assign(modules);
	m_Port = port;

	m_Pfd.resize(m_Modules.size());
	for (auto &pf : m_Pfd)
	{
		pf.fd = -1;
		pf.events = POLLIN;
	}

	std::cout << "Connecting to the TCP server..." << std::endl;

	for (char c : modules)
	{
		if (Connect(c))
		{
			return true;
		}
	}
	return false;
}

bool CTCClient::Connect(char module)
{
	const auto pos = m_Modules.find(module);
	if (pos == std::string::npos)
	{
		std::cerr << "CTCClient::Connect: could not find module '" << module << "' in configured modules!" << std::endl;
		return true;
	}
	CIp ip(m_Address.c_str(), AF_UNSPEC, SOCK_STREAM, m_Port);

	auto fd = socket(ip.GetFamily(), SOCK_STREAM, 0);
	if (fd < 0)
	{
		std::cerr << "Could not open socket for module '" << module << "'" << std::endl;
		perror("TC client socket");
		return true;
	}

	int yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)))
	{
		std::cerr << "Moudule " << module << " error:";
		perror("setsockopt");
		close(fd);
		return true;
	}

	unsigned count = 0;
	while (connect(fd, ip.GetCPointer(), ip.GetSize()))
	{
		if (ECONNREFUSED == errno)
		{
			if (0 == ++count % 100) std::cout << "Connection refused! Restart the reflector." << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		else
		{
			std::cerr << "Module " << module << " error: ";
			perror("connect");
			close(fd);
			return true;
		}
	}

	int sent = send(fd, &module, 1, 0); // send the identification byte
	if (sent < 0)
	{
		std::cerr << "Error sending ID byte to module '" << module << "':" << std::endl;
		perror("send");
		close(fd);
		return true;
	}
	else if (0 == sent)
	{
		std::cerr << "Could not set ID byte to module '" << module << "'" << std::endl;
		close(fd);
		return true;
	}

	// Enable TCP keepalive to detect dead connections
	int keepalive = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
	int idle = 10;      // start probes after 10s idle
	int interval = 5;   // probe every 5s
	int cnt = 3;        // 3 failed probes = dead
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

	{ std::ostringstream s; s << "File descriptor " << fd << " on " << ip << " opened for module '" << module << "'"; std::cout << s.str() << std::endl; }

	m_Pfd[pos].fd = fd;

	return false;
}

void CTCClient::ReConnect() // and sometimes ping
{
	static std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
	auto now = std::chrono::system_clock::now();
	std::chrono::duration<double> secs = now - start;

	// Probe all sockets to detect dead connections before checking FDs
	for (char m : m_Modules)
		IsModuleConnected(m);

	for (char m : m_Modules)
	{
		if (0 > GetFD(m))
		{
			std::cout << "Reconnecting module " << m << "..." << std::endl;
			if (Connect(m))
			{
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
		}
	}
	
	if(secs.count() > 5.0)
	{
		STCPacket ping;
		ping.codec_in = ECodecType::ping;
		Send(&ping);
		start = now;
	}
}

void CTCClient::Receive(std::queue<std::unique_ptr<STCPacket>> &queue, int ms)
{
	for (auto &pfd : m_Pfd)
		pfd.revents = 0;

	auto rv = poll(m_Pfd.data(), m_Pfd.size(), ms);

	if (rv < 0)
	{
		perror("Receive poll");
		return;
	}

	if (0 == rv)
		return;

	for (auto &pfd : m_Pfd)
	{
		if (pfd.fd < 0)
			continue;

		if (pfd.revents & POLLIN)
		{
			auto p_tcpack = std::make_unique<STCPacket>();
			if (receive(pfd.fd, p_tcpack.get()))
			{
				p_tcpack.reset();
				Close(pfd.fd);
			}
			else
			{
				queue.push(std::move(p_tcpack));
			}
		}

		if (pfd.revents & POLLERR || pfd.revents & POLLHUP)
		{
			std::cerr << "Connection lost on module " << GetMod(pfd.fd) << ", reconnecting..." << std::endl;
			Close(pfd.fd);
		}
		if (pfd.revents & POLLNVAL)
		{
			pfd.fd = -1;
		}
	}
}
