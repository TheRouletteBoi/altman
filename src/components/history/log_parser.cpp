#define _CRT_SECURE_NO_WARNINGS
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <regex>

#include "log_parser.h"

using namespace std;
namespace fs = filesystem;

string logsFolder() {
	const char *localAppDataPath = getenv("LOCALAPPDATA");
	return localAppDataPath ? string(localAppDataPath) + "\\Roblox\\logs" : string{};
}

void parseLogFile(LogInfo &logInfo) {
	using namespace string_view_literals;

	constexpr size_t kMaxRead = 512 * 1024; // halfMB
	ifstream fileInputStream(logInfo.fullPath, ios::binary);
	if (!fileInputStream)
		return;

	string fileBuffer(kMaxRead, '\0');
	fileInputStream.read(fileBuffer.data(), kMaxRead);
	fileBuffer.resize(static_cast<size_t>(fileInputStream.gcount()));
	string_view log_data_view(fileBuffer);

	auto nextLinePos = [&log_data_view](size_t currentPosition) -> size_t {
		size_t newlinePosition = log_data_view.find('\n', currentPosition);
		return newlinePosition == string_view::npos ? log_data_view.size() : newlinePosition;
	};

	for (size_t currentScanPosition = 0; currentScanPosition < log_data_view.size();) {
		size_t endOfLineIndex = nextLinePos(currentScanPosition);
		string_view currentLineView = log_data_view.substr(currentScanPosition, endOfLineIndex - currentScanPosition);

		if (!currentLineView.empty() && currentLineView.back() == '\r') {
			currentLineView.remove_suffix(1);
		}

		if (logInfo.timestamp.empty() && currentLineView.length() >= 20 && !currentLineView.empty() && isdigit(
			    currentLineView[0])) {
			size_t timestampZIndex = currentLineView.find('Z');
			if (timestampZIndex != string_view::npos && timestampZIndex < 30) {
				// Ensure Z is reasonably placed for a timestamp
				logInfo.timestamp = string(currentLineView.substr(0, timestampZIndex + 1));
			}
		}

		// Still collect output lines for compatibility with data saving/loading
		if (currentLineView.find("[FLog::Output]"sv) != string_view::npos) {
			logInfo.outputLines.emplace_back(currentLineView);
		}

		if (logInfo.channel.empty()) {
			constexpr auto token = "The channel is "sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find_first_of(" \t\n\r"sv, valueStartIndex);
				logInfo.channel = string(currentLineView.substr(valueStartIndex,
				                                                (valueEndIndex == string_view::npos
					                                                 ? currentLineView.length()
					                                                 : valueEndIndex) -
				                                                valueStartIndex));
			}
		}

		if (logInfo.version.empty()) {
			constexpr auto token = "\"version\":\""sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find('"', valueStartIndex);
				if (valueEndIndex != string_view::npos)
					logInfo.version = string(currentLineView.substr(valueStartIndex, valueEndIndex - valueStartIndex));
			}
		}

		if (logInfo.joinTime.empty()) {
			constexpr auto token = "join_time:"sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find_first_not_of("0123456789."sv, valueStartIndex);
				logInfo.joinTime = string(currentLineView.substr(valueStartIndex,
				                                                 (valueEndIndex == string_view::npos
					                                                  ? currentLineView.length()
					                                                  : valueEndIndex) -
				                                                 valueStartIndex));
			}
		}

		if (logInfo.jobId.empty()) {
			static const regex s_guid_regex(R"([0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12})");
			constexpr auto token = "Joining game '"sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find('\'', valueStartIndex); // Find closing quote
				if (valueEndIndex != string_view::npos) {
					string_view guidCandidateView = currentLineView.substr(
						valueStartIndex, valueEndIndex - valueStartIndex);
					if (regex_match(guidCandidateView.begin(), guidCandidateView.end(), s_guid_regex)) {
						logInfo.jobId = string(guidCandidateView);
					}
				}
			}
		}

		if (logInfo.placeId.empty()) {
			constexpr auto token = "place "sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find_first_not_of("0123456789"sv, valueStartIndex);
				logInfo.placeId = string(currentLineView.substr(valueStartIndex,
				                                                (valueEndIndex == string_view::npos
					                                                 ? currentLineView.length()
					                                                 : valueEndIndex) -
				                                                valueStartIndex));
			}
		}

		if (logInfo.universeId.empty()) {
			constexpr auto token = "universeid:"sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find_first_not_of("0123456789"sv, valueStartIndex);
				logInfo.universeId = string(currentLineView.substr(valueStartIndex,
				                                                   (valueEndIndex == string_view::npos
					                                                    ? currentLineView.length()
					                                                    : valueEndIndex) -
				                                                   valueStartIndex));
			}
		}

		if (logInfo.serverIp.empty()) {
			constexpr auto token = "UDMUX Address = "sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find(", Port = "sv, valueStartIndex);
				if (valueEndIndex != string_view::npos) {
					logInfo.serverIp = string(currentLineView.substr(valueStartIndex, valueEndIndex - valueStartIndex));
					constexpr auto portPrefixToken = ", Port = "sv;
					size_t portValueStartIndex = valueEndIndex + portPrefixToken.length();
					auto portValueEndIndex = currentLineView.find_first_not_of("0123456789"sv, portValueStartIndex);
					logInfo.serverPort = string(currentLineView.substr(portValueStartIndex,
					                                                   (portValueEndIndex == string_view::npos
						                                                    ? currentLineView.length()
						                                                    : portValueEndIndex) -
					                                                   portValueStartIndex));
				}
			}
		}

		if (logInfo.userId.empty()) {
			constexpr auto token = "userId = "sv;
			auto tokenIndex = currentLineView.find(token);
			if (tokenIndex != string_view::npos) {
				size_t valueStartIndex = tokenIndex + token.length();
				auto valueEndIndex = currentLineView.find_first_not_of("0123456789"sv, valueStartIndex);
				logInfo.userId = string(currentLineView.substr(valueStartIndex,
				                                               (valueEndIndex == string_view::npos
					                                                ? currentLineView.length()
					                                                : valueEndIndex) -
				                                               valueStartIndex));
			}
		}

		currentScanPosition = endOfLineIndex + 1;
	}
}
