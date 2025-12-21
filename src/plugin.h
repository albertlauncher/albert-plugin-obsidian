// Copyright (c) 2025-2025 Manuel Schneider

#pragma once
#include <QFileSystemWatcher>
#include <albert/extensionplugin.h>
#include <albert/indexqueryhandler.h>
#include <memory>
#include <vector>
class VaultItem;

class Plugin : public albert::ExtensionPlugin,
               public albert::IndexQueryHandler
{
    ALBERT_PLUGIN

public:

    Plugin();

    void updateIndexItems() override;
    std::vector<albert::RankItem> rankItems(albert::QueryContext &) override;

    QFileSystemWatcher watcher;
    std::vector<std::shared_ptr<VaultItem>> vaults;

};
