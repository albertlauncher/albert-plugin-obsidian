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
#include <albert/albert.h>
#include <albert/iconutil.h>
#include <albert/logging.h>
#include <albert/networkutil.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
ALBERT_LOGGING_CATEGORY("obsidian")
class NoteItem;
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

class VaultItem : public QObject,
                  public enable_shared_from_this<VaultItem>,
                  public albert::Item
{
    Q_OBJECT

public:
    const QString identifier;
    const QString path;

    VaultItem(const QString &identifier_, const QString &path_):
        identifier(identifier_),
        path(path_)
    {
        QObject::connect(&watcher_, &QFileSystemWatcher::directoryChanged,
                         this, &VaultItem::onDirectoryChanged);
    }

    inline QString getName() const { return QFileInfo(path).fileName(); }

    inline QString getLocation() const { return QFileInfo(path).filePath(); }

    const vector<shared_ptr<NoteItem>> &notes() const { return notes_; }

    void onDirectoryChanged(); // Out of line. requires NoteItem definition

    QString id() const override { return identifier; }

    QString text() const override { return getName(); }

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

private:
    QFileSystemWatcher watcher_;
    vector<shared_ptr<NoteItem>> notes_;

signals:
    void notesChanged();
};


class NoteItem : public albert::Item
{
public:
    const shared_ptr<VaultItem> vault;
    const QString path;

    NoteItem(shared_ptr<VaultItem> vault_, const QString &path_):
        vault(vault_), path(path_) {}

    QString id() const override
    { return vault->path + path; }

    QString text() const override
    { return QFileInfo(path).completeBaseName(); }

    QString subtext() const override
    { return u"%1 · %2"_s.arg(vault->getName(), path); }

    unique_ptr<Icon> icon() const override
    { return makeImageIcon(u":obsidian-note"_s); }

    vector<Action> actions() const override
    {
        return {{u"open"_s,
                 Plugin::tr("Open"),
                 [this]{
                     openUrl(u"obsidian://open?vault=%1&file=%2"_s
                                 .arg(percentEncoded(vault->identifier),
                                      percentEncoded(path)));
                 }
        }};
    }
};


void VaultItem::onDirectoryChanged()
{
    // Update watches

    if (const auto dirs = watcher_.directories();
        !dirs.isEmpty())
        watcher_.removePaths(dirs);

    QStringList dirs;
    QDirIterator dit(path, QDir::NoDotAndDotDot | QDir::Dirs, QDirIterator::Subdirectories);
    while (dit.hasNext())
        dirs << dit.next();
    dirs << path;

    if (!dirs.isEmpty())
        watcher_.addPaths(dirs);

    // Build note items

    INFO << "Indexing Obsidian notes in" << path << identifier;

    notes_.clear();

    for (const auto &dir : as_const(dirs))
        for (const auto files = QDir(dir).entryList(QDir::Files);
             const auto &file : files)
        {
            const auto relative_path = QDir(path).relativeFilePath(QDir(dir).filePath(file));
            notes_.emplace_back(make_shared<NoteItem>(shared_from_this(), relative_path));
        }

    emit notesChanged();
}


// -------------------------------------------------------------------------------------------------

static vector<shared_ptr<VaultItem>> getVaults()
{
    auto obsidian_json =
#ifdef Q_OS_MAC
        albert::dataLocation().parent_path();
#elifdef Q_OS_UNIX
        albert::configLocation().parent_path();
    if (!exists(obsidian_json))
        obsidian_json = QDir::home().filesystemPath() / ".var" / "app" / "md.obsidian.Obsidian" / "config";
#endif
    obsidian_json = obsidian_json / "obsidian" / "obsidian.json";

    if (!exists(obsidian_json))
        throw runtime_error("Obsidian JSON file not found at " + obsidian_json.string());

    vector<shared_ptr<VaultItem>> vaults;

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

    for (const auto &v : vaults)
        DEBG << "Found vault:" << v->getName() << v->path;

    return vaults;
}

Plugin::Plugin() : vaults(getVaults())  // Throws
{
    for (auto &vault : vaults)
    {
        vault->onDirectoryChanged();
        connect(vault.get(), &VaultItem::notesChanged,
                this, &Plugin::updateIndexItems);
    }
}

void Plugin::updateIndexItems()
{
    vector<IndexItem> r;
    for (const auto &v : vaults)
    {
        r.emplace_back(v, v->getName());
        for (const auto &note : v->notes())
        {
            r.emplace_back(note, note->text());  // index by name
            r.emplace_back(note, note->path);  // index by path
        }
    }

    setIndexItems(::move(r));
}

void Plugin::handleTriggerQuery(albert::Query &query)
{
    GlobalQueryHandler::handleTriggerQuery(query);

    const auto trimmed = query.string().trimmed();
    if (!trimmed.isEmpty())
        for (const auto &v : vaults)
            query.add(StandardItem::make(
                u"new"_s,
                tr("Create new note in '%1'").arg(v->getName()),
                u"%1 · %2"_s.arg(QFileInfo(v->getName()).filePath(), trimmed + u".md"_s),
                []{ return makeImageIcon(u":obsidian-note-add"_s); },
                {{
                    u"create"_s,
                    tr("Create"),
                    [v, trimmed] {
                        openUrl(u"obsidian://new?vault=%1&file=%2"_s
                                  .arg(percentEncoded(v->id()),
                                       percentEncoded(trimmed)));
                    }
                }},
                u""_s  // disable completion
            ));
}

#include "plugin.moc"
