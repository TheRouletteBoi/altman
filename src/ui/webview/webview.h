#pragma once

#include <functional>
#include <string>

#include "components/data.h"
#include "ui/webview/webview.h"

struct LoginCredentials {
    std::string cookie;
    std::string password;
};

using CredentialsCallback = std::function<void(const LoginCredentials &)>;

void LaunchWebviewImpl(
    const std::string &url,
    const std::string &windowName = "Altman Webview",
    const std::string &cookie = "",
    const std::string &userId = "",
    CredentialsCallback onCredentialsExtracted = nullptr
);

void LaunchWebview(const std::string &url, const AccountData &account);

void LaunchWebview(const std::string &url, const AccountData &account, const std::string &windowName);

void LaunchWebviewForLogin(const std::string &url, const std::string &windowName, CredentialsCallback onCredentialsExtracted);
