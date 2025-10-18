// Copyright (c) 2025-2025 Manuel Schneider

#include "plugin.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <albert/app.h>
#include <albert/iconutil.h>
#include <albert/logging.h>
#include <albert/networkutil.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <ranges>
ALBERT_LOGGING_CATEGORY("obsidian")
class NoteItem;
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

class VaultItem : public albert::Item
{
public:
    const QString identifier;
    const QString path;
    const QString name;

    VaultItem(const QString &id, const QFileInfo &file_info):
        identifier(id),
        path(file_info.filePath()),
        name(file_info.fileName())
    {}

    QString id() const override { return identifier; }

    QString text() const override { return name; }

    QString subtext() const override { return path; }

    unique_ptr<Icon> icon() const override { return makeImageIcon(u":obsidian-vault"_s); }

    vector<Action> actions() const override
    {
        vector<Action> actions;
        actions.emplace_back(
            u"open"_s, Plugin::tr("Open"),
            [this] { openUrl(u"obsidian://open?vault=%1"_s.arg(percentEncoded(identifier))); }
            );
        actions.emplace_back(
            u"search"_s, Plugin::tr("Search"),
            [this] { openUrl(u"obsidian://search?vault=%1"_s.arg(percentEncoded(identifier))); }
            );
        actions.emplace_back(
            u"openfm"_s, Plugin::tr("Open in file manager"),
            [this] { open(path); }
            );
        return actions;
    }
};


class NoteItem : public albert::Item
{
public:
    const shared_ptr<VaultItem> vault;
    const QString relative_path;

    NoteItem(shared_ptr<VaultItem> v, const QString &file_path) :
        vault(v),
        relative_path(QDir(v->path).relativeFilePath(file_path))
    {}

    QString id() const override
    { return vault->path + relative_path; }

    QString text() const override
    { return QFileInfo(relative_path).completeBaseName(); }

    QString subtext() const override
    { return u"%1 · %2"_s.arg(vault->name, relative_path); }

    unique_ptr<Icon> icon() const override
    { return makeImageIcon(u":obsidian-note"_s); }

    vector<Action> actions() const override
    {
        return {{u"open"_s,
                 Plugin::tr("Open"),
                 [this]{
                     openUrl(u"obsidian://open?vault=%1&file=%2"_s
                                 .arg(percentEncoded(vault->identifier),
                                      percentEncoded(relative_path)));
                 }
        }};
    }
};

// -------------------------------------------------------------------------------------------------

filesystem::path obsidianConfigPath()
{
    auto obsidian_json =
#ifdef Q_OS_MAC
        App::dataLocation().parent_path();
#elifdef Q_OS_UNIX
        App::configLocation().parent_path();
    if (!exists(obsidian_json))
        obsidian_json = QDir::home().filesystemPath() / ".var" / "app" / "md.obsidian.Obsidian" / "config";
#endif
    obsidian_json = obsidian_json / "obsidian" / "obsidian.json";
    return obsidian_json;
}

static vector<shared_ptr<VaultItem>> readVaults(filesystem::path obsidian_json)
{
    decltype(readVaults(filesystem::path())) vaults;

    QFile file(obsidian_json);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);

        if (error.error != QJsonParseError::NoError)
            WARN << u"Failed to parse Obsidian JSON file: "_s << error.errorString();

        const auto vaults_object = doc.object().value("vaults"_L1).toObject();

        for (auto it = vaults_object.begin(); it != vaults_object.end(); ++it)
            vaults.emplace_back(make_shared<VaultItem>(
                it.key(), QFileInfo(it.value()["path"_L1].toString())));
    }

    return vaults;
}

Plugin::Plugin()
{
    if (const auto config = obsidianConfigPath(); !exists(config))
        throw runtime_error("Obsidian JSON file not found at " + config.string());

    connect(&watcher, &QFileSystemWatcher::directoryChanged,
            this, &Plugin::updateIndexItems);
}

static shared_ptr<Item> makeAddNoteItem(const VaultItem &v, const QString &path)
{
    return StandardItem::make(
        u"new"_s,
        Plugin::tr("Create new note in '%1'").arg(v.name),
        u"%1 · %2"_s.arg(QFileInfo(v.name).filePath(), path + u".md"_s),
        [] { return makeImageIcon(u":obsidian-note-add"_s); },
        {{u"create"_s,
          Plugin::tr("Create"),
          [v, path] {
              openUrl(u"obsidian://new?vault=%1&file=%2"_s.arg(percentEncoded(v.id()),
                                                               percentEncoded(path)));
          }}},
        u""_s  // disable completion
    );
}

vector<RankItem> Plugin::handleGlobalQuery(Query &query)
{
    vector<RankItem> matches = IndexQueryHandler::handleGlobalQuery(query);

    if (!query.trigger().isEmpty())
        if (const auto trimmed = query.string().trimmed();
            !trimmed.isEmpty())
        {
            auto view = vaults
                        | views::transform([&](auto &vault)
                                           { return RankItem(makeAddNoteItem(*vault, trimmed), .0); });
            matches.append_range(view);
        }

    return matches;
}

void Plugin::updateIndexItems()
{
    vaults = readVaults(obsidianConfigPath());
    vector<shared_ptr<NoteItem>> notes;
    QStringList watched_dirs;

    for (auto &vault : vaults)
    {
        QDirIterator dit(vault->path,
                         QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files,
                         QDirIterator::Subdirectories);

        while (dit.hasNext())
        {
            auto file_info = dit.nextFileInfo();
            if (file_info.isDir())
                watched_dirs << file_info.filePath();
            else if (/*file_info.isFile() && */file_info.suffix().toLower() == u"md"_s)
                notes.emplace_back(make_shared<NoteItem>(vault, file_info.filePath()));
        }
    }


    if (const auto dirs = watcher.directories(); !dirs.isEmpty())
        watcher.removePaths(dirs);
    if (!watched_dirs.isEmpty())
        watcher.addPaths(watched_dirs);

    vector<IndexItem> r;
    for (const auto &v : vaults)
    {
        r.emplace_back(v, v->name);
        for (const auto &note : notes)
        {
            r.emplace_back(note, note->text());  // index by name
            r.emplace_back(note, note->relative_path);  // index by path
        }
    }

    setIndexItems(::move(r));
}
