// Copyright (c) 2025-2025 Manuel Schneider

#pragma once
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>
#include <vector>
#include <memory>
class VaultItem;

class Plugin : public albert::ExtensionPlugin,
               public albert::IndexQueryHandler
{
    ALBERT_PLUGIN

public:

    Plugin();

    void updateIndexItems() override;
    void handleThreadedQuery(albert::ThreadedQuery &) override;

    const std::vector<std::shared_ptr<VaultItem>> vaults;

};
