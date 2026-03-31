// LogBuffer -- Thread-safe circular log buffer for admin interface

#include <unistd.h>
#include <ctime>
#include <cstdio>
#include "LogBuffer.h"

CLogBuffer g_LogBuffer;

CLogBuffer::CLogBuffer()
	: m_Head(0)
	, m_Count(0)
	, m_OrigBuf(nullptr)
{
	m_Lines.resize(LOG_BUFFER_SIZE);
}

CLogBuffer::~CLogBuffer()
{
	Uninstall();
}

void CLogBuffer::Install(void)
{
	m_OrigBuf = std::cout.rdbuf(this);
}

void CLogBuffer::Uninstall(void)
{
	if (m_OrigBuf)
	{
		std::cout.rdbuf(m_OrigBuf);
		m_OrigBuf = nullptr;
	}
}

int CLogBuffer::overflow(int c)
{
	if (c != EOF)
	{
		AddChar((char)c);
		// Write directly to stdout fd (bypasses rdbuf, works with supervisord)
		char ch = (char)c;
		::write(STDOUT_FILENO, &ch, 1);
	}
	return c;
}

std::streamsize CLogBuffer::xsputn(const char *s, std::streamsize n)
{
	for (std::streamsize i = 0; i < n; i++)
		AddChar(s[i]);

	// Write directly to stdout fd
	::write(STDOUT_FILENO, s, n);

	return n;
}

void CLogBuffer::AddChar(char c)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (c == '\n')
	{
		// Prepend timestamp to stored line
		char ts[20];
		time_t now = time(nullptr);
		struct tm *t = localtime(&now);
		snprintf(ts, sizeof(ts), "%02d:%02d:%02d ", t->tm_hour, t->tm_min, t->tm_sec);

		m_Lines[m_Head] = std::string(ts) + m_Current;
		m_Head = (m_Head + 1) % LOG_BUFFER_SIZE;
		if (m_Count < LOG_BUFFER_SIZE)
			m_Count++;
		m_Current.clear();
	}
	else
	{
		m_Current += c;
	}
}

std::vector<std::string> CLogBuffer::GetLines(int count) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (count > m_Count)
		count = m_Count;

	std::vector<std::string> result;
	result.reserve(count);

	// Start from oldest requested line
	int start = (m_Head - count + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
	for (int i = 0; i < count; i++)
	{
		result.push_back(m_Lines[(start + i) % LOG_BUFFER_SIZE]);
	}

	return result;
}
