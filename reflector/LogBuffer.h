#pragma once

// LogBuffer -- Thread-safe circular log buffer for admin interface
// Captures urfd console output for live log viewing.

#include <string>
#include <vector>
#include <mutex>
#include <sstream>
#include <iostream>

#define LOG_BUFFER_SIZE 200

class CLogBuffer : public std::streambuf
{
public:
	CLogBuffer();
	~CLogBuffer();

	// Install: redirect std::cout through this buffer
	void Install(void);
	void Uninstall(void);

	// Get last N lines
	std::vector<std::string> GetLines(int count) const;

protected:
	// streambuf overrides
	int overflow(int c) override;
	std::streamsize xsputn(const char *s, std::streamsize n) override;

private:
	void AddChar(char c);

	mutable std::mutex m_Mutex;
	std::vector<std::string> m_Lines;
	int m_Head;          // next write position
	int m_Count;         // number of valid lines
	std::string m_Current;  // line being built

	std::streambuf *m_OrigBuf;  // original cout buffer
};

extern CLogBuffer g_LogBuffer;
