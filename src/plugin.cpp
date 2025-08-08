// Copyright (c) 2025-2025 Manuel Schneider

#include "plugin.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <albert/logging.h>
#include <albert/albert.h>
#include <albert/systemutil.h>
#include <albert/standarditem.h>
ALBERT_LOGGING_CATEGORY("obsidian")
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

class VaultItem : public albert::Item
{
public:
    QString id_;
    QString name_;
    QString location_;

    VaultItem(const QString &id, const QString &path) : id_(id)
    {
        name_ = QFileInfo(path).fileName();
        location_ = QFileInfo(path).absolutePath();
    }

    QString path() const { return QDir(location_).filePath(name_); }

    QString id() const override { return id_; }
    QString text() const override { return name_; }
    QString subtext() const override { return u"%1 · %2"_s.arg(Plugin::tr("Vault"), path()); }
    QStringList iconUrls() const override { return {u":obsidian-vault"_s}; }
    vector<Action> actions() const override
    {
        vector<Action> actions;
        actions.emplace_back(
            u"open"_s, Plugin::tr("Open"),
            [this] { openUrl(u"obsidian://open?vault=%1"_s.arg(QUrl::toPercentEncoding(id_))); }
        );
        actions.emplace_back(
            u"search"_s, Plugin::tr("Search"),
            [this] { openUrl(u"obsidian://search?vault=%1"_s.arg(QUrl::toPercentEncoding(id_))); }
        );
        return actions;
    }
};

class NoteItem : public albert::Item
{
public:
    shared_ptr<VaultItem> vault_;
    QString file_name_;
    QString relative_parent_path_;  // relative to the root/vault, without filename

    NoteItem(shared_ptr<VaultItem> vault, const QString &relative_path) : vault_(vault)
    {
        const auto file_info = QFileInfo(relative_path);
        file_name_ = file_info.fileName();
        relative_parent_path_ = file_info.path();
    }

    QString id() const override { return vault_->id_ + relative_parent_path_ + file_name_; }
    QString text() const override { return file_name_; }
    QString subtext() const override
    {
        if (relative_parent_path_ == u"."_s)
            return u"%1 · %2"_s.arg(Plugin::tr("Note"), vault_->name_);
        else
            return u"%1 · %2 → %3"_s
                .arg(Plugin::tr("Note"),
                     vault_->name_,
                     relative_parent_path_);
    }
    QStringList iconUrls() const override { return {u":obsidian-note"_s}; }
    vector<Action> actions() const override
    {
        return {{
            u"open"_s,
            Plugin::tr("Open"),
            [this]{
                const auto url = u"obsidian://open?vault=%1&file=%2"_s
                                          .arg(QUrl::toPercentEncoding(vault_->id_),
                                               QUrl::toPercentEncoding(QDir(relative_parent_path_).filePath(file_name_)));
                     DEBG << url;
                openUrl(url);
            }}};
    }
};

// -------------------------------------------------------------------------------------------------

static vector<shared_ptr<VaultItem>> getVaults()
{
    vector<shared_ptr<VaultItem>> vaults;
    auto obsidian_json = albert::dataLocation().parent_path() / "obsidian" / "obsidian.json";

    if (!exists(obsidian_json))
        throw runtime_error("Obsidian JSON file not found at " + obsidian_json.string());

    QFile file(obsidian_json);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);

        if (error.error != QJsonParseError::NoError)
            throw runtime_error("Failed to parse Obsidian JSON file: "
                                + error.errorString().toStdString());

        const auto vaults_object = doc.object().value("vaults"_L1).toObject();
        for (auto it = vaults_object.begin(); it != vaults_object.end(); ++it)
            vaults.emplace_back(make_shared<VaultItem>(it.key(), it.value()["path"_L1].toString()));
    }

    return vaults;
}

Plugin::Plugin() : vaults(getVaults()) // Throws
{}

void Plugin::updateIndexItems()
{
    INFO << "Indexing Obsidian vaults";

    vector<IndexItem> r;
    QStringList dir_list;

    for (const auto &v : vaults)
    {
        const auto vault_path = QDir(v->location_).filePath(v->name_);

        r.emplace_back(v, v->name_);  // The vault itself

        QDirIterator dirIt(vault_path,
                           QDir::NoDotAndDotDot|QDir::Files|QDir::Dirs,
                           QDirIterator::Subdirectories);

        while (dirIt.hasNext())
        {
            dirIt.next();
            if (const auto fi = dirIt.fileInfo(); fi.isDir())
                dir_list << fi.absoluteFilePath();
            else
            {
                const auto rel_path = QDir(vault_path).relativeFilePath(fi.filePath());
                const auto item = make_shared<NoteItem>(v, rel_path);
                r.emplace_back(item, item->file_name_);   // index by name
                r.emplace_back(item, rel_path);  // index by path
            }
        }

        // Add the vault directory itself to the list of directories to watch
        dir_list << QDir(vault_path).absolutePath();
    }

    if (const auto dirs = watcher.directories(); !dirs.isEmpty())
        watcher.removePaths(dirs);

    if (!dir_list.isEmpty())
        watcher.addPaths(dir_list);

    setIndexItems(::move(r));

    connect(&watcher, &QFileSystemWatcher::directoryChanged,
            this, &Plugin::updateIndexItems, Qt::SingleShotConnection);
}

void Plugin::handleTriggerQuery(albert::Query &query)
{
    GlobalQueryHandler::handleTriggerQuery(query);

    const auto trimmed = query.string().trimmed();
    if (!trimmed.isEmpty())
    {
        for (const auto &v : vaults)
            query.add(StandardItem::make(
                u"new"_s,
                tr("Create new note in vault '%1'").arg(v->name_),
                QDir(v->path()).filePath(trimmed + u".md"),
                {u":obsidian-note-add"_s},
                {{
                    u"create"_s,
                    tr("Create"),
                    [v, trimmed] {
                        openUrl(u"obsidian://new?vault=%1&name=%2"_s
                                        .arg(QUrl::toPercentEncoding(v->id_),
                                             QUrl::toPercentEncoding(trimmed)));
                    }
                }},
                u""_s  // disable completion
            ));
    }
}
