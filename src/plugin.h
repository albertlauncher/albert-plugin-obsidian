// Copyright (c) 2025-2025 Manuel Schneider

#pragma once
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>
#include <vector>
#include <memory>
class VaultItem;

class Plugin : public albert::util::ExtensionPlugin,
               public albert::util::IndexQueryHandler
{
    ALBERT_PLUGIN

public:

    Plugin();

    void updateIndexItems() override;
    void handleTriggerQuery(albert::Query &) override;

    const std::vector<std::shared_ptr<VaultItem>> vaults;

};
