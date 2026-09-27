#include "remarkablefile.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

namespace {
struct Failure { int code; QByteArray message; };
[[noreturn]] void fail(int code, const QString &message) { throw Failure{code, message.toUtf8()}; }
QByteArray lastError, rootBytes;
QString root;
std::recursive_mutex mutex;
struct Entry { QString id, parent, name, ext, label; QJsonObject meta; QByteArray raw; bool dir; };
struct Pending { QString id, parent, name, ext, file; };
QHash<QString, Entry> entries;
QHash<QString, Pending> pending;

template<class F> int call(F fn) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try { lastError.clear(); return fn(); }
    catch (const Failure &e) { errno = e.code; lastError = e.message; return -1; }
    catch (...) { errno = EIO; lastError = "library operation failed"; return -1; }
}
QString path(const char *input) {
    QStringList out;
    for (const auto &part : QString::fromUtf8(input).split('/', Qt::SkipEmptyParts)) {
        if (part == "." || part == ".." || part.contains(QChar::Null)) fail(EINVAL, "Invalid path");
        out << part;
    }
    return out.join('/');
}
QString file(const QString &id, const QString &suffix) { return root + '/' + id + suffix; }
QByteArray read(const QString &name) {
    QFile f(name);
    if (QFileInfo(name).isSymLink() || !f.open(QIODevice::ReadOnly)) fail(EIO, "Cannot read " + name);
    if (f.size() > 16*1024*1024) fail(EFBIG, "Metadata too large");
    return f.readAll();
}
QJsonObject json(const QByteArray &raw) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail(EINVAL, "Invalid JSON metadata");
    return doc.object();
}
void save(const QString &name, const QByteArray &bytes) {
    if (QFileInfo(name).isSymLink()) fail(ELOOP, "Metadata symlink rejected");
    QSaveFile out(name);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        fail(EIO, "Cannot commit " + name);
}
QString display(QString name) {
    QString out;
    for (QChar c : name) {
        if (c.unicode() < 32 || QString("%/\\:*?\"<>|").contains(c))
            out += '%' + QString::number(c.unicode(), 16).rightJustified(2, '0').toUpper();
        else out += c;
    }
    if (out.isEmpty() || out == "." || out == "..") out = "Untitled";
    while (out.toUtf8().size() > 170) out.chop(1);
    return out;
}
void scan() {
    entries.clear();
    // ponytail: rescan JSON for each directory operation; cache by file stamp if large libraries need it.
    for (const auto &name : QDir(root).entryList({"*.metadata"}, QDir::Files, QDir::Name)) {
        QString id = name.chopped(9);
        if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id) continue;
        try {
            auto raw = read(file(id, ".metadata")); auto m = json(raw);
            if (m["deleted"].toBool() || m["parent"] == "trash") continue;
            bool dir = m["type"] == "CollectionType";
            QString ext;
            if (!dir) {
                if (m["type"] != "DocumentType") continue;
                ext = json(read(file(id, ".content")))["fileType"].toString().toLower();
                if (ext != "pdf" && ext != "epub") continue;
                QFileInfo data(file(id, '.' + ext));
                if (!data.isFile() || data.isSymLink()) continue;
            }
            Entry e{id,m["parent"].toString(),m["visibleName"].toString(),ext,{},m,raw,dir};
            e.label = display(e.name) + (dir ? "" : '.' + ext);
            entries.insert(id,e);
        } catch (const Failure &) { /* An unrelated malformed document does not hide the rest. */ }
    }
    QHash<QString,int> counts;
    for (auto e : entries) ++counts[e.parent + '/' + e.label];
    for (auto &e : entries) {
        if (counts[e.parent + '/' + e.label] > 1 || e.name.toUtf8().size() > 170)
            e.label = display(e.name) + " [" + e.id + "]" + (e.dir ? "" : '.' + e.ext);
    }
}
Entry resolve(const QString &p) {
    QString parent;
    Entry found;
    for (const auto &part : p.split('/',Qt::SkipEmptyParts)) {
        bool ok = false;
        for (auto e : entries) if (e.parent == parent && (e.label == part || (!e.dir && part.size()>e.ext.size() && e.label.chopped(e.ext.size()) == part.chopped(e.ext.size()) && part.endsWith(e.ext,Qt::CaseInsensitive)))) { found=e; ok=true; break; }
        if (!ok) fail(ENOENT, "Library entry not found: " + p);
        parent=found.id;
    }
    if (parent.isEmpty()) fail(EINVAL, "Operation requires a document or folder");
    return found;
}
QString parentFor(const QString &p) {
    auto parts=p.split('/'); parts.removeLast();
    if (parts.isEmpty()) return {};
    auto e=resolve(parts.join('/'));
    if (!e.dir) fail(ENOTDIR,"Parent is not a folder");
    return e.id;
}
void unused(const QString &p) {
    try { resolve(p); } catch (const Failure &e) { if (e.code==ENOENT) return; throw; }
    fail(EEXIST,"An entry with this name already exists");
}
void touch(QJsonObject &m) {
    m["version"]=m["version"].toInt()+1;
    m["lastModified"]=QString::number(QDateTime::currentMSecsSinceEpoch());
    m["modified"]=true; m["metadatamodified"]=true; m["synced"]=false;
}
void update(Entry e, QJsonObject m) {
    // Detect concurrent changes observed since this operation read the metadata.
    if (read(file(e.id,".metadata")) != e.raw) fail(EBUSY,"Document changed concurrently; retry");
    touch(m); save(file(e.id,".metadata"),QJsonDocument(m).toJson());
}
QJsonObject metadata(const QString &id,const QString &parent,const QString &name,bool dir) {
    QJsonObject m{{"id",id},{"parent",parent},{"visibleName",name},{"type",dir?"CollectionType":"DocumentType"},
        {"deleted",false},{"pinned",false},{"version",0}};
    touch(m); return m;
}
void checkTitle(const QString &name) {
    if(name.isEmpty() || display(name)!=name) fail(EINVAL,"Title must fit 170 UTF-8 bytes and use ordinary filename characters");
}
QString suffix(const QString &p) {
    auto ext=QFileInfo(p).suffix().toLower();
    if (ext!="pdf" && ext!="epub") fail(ENOTSUP,"Library accepts PDF and EPUB only");
    return ext;
}
void validate(const Pending &p) {
    QFile f(p.file);
    if (!f.open(QIODevice::ReadOnly)) fail(EIO,"Cannot read uploaded document");
    auto head=f.read(1024);
    if (p.ext=="pdf") {
        if (!head.contains("%PDF-")) fail(EINVAL,"Invalid PDF header");
        f.seek(qMax<qint64>(0,f.size()-4096));
        if (!f.readAll().contains("%%EOF")) fail(EINVAL,"Incomplete PDF");
    } else {
        auto *a=archive_read_new(); archive_read_support_format_zip(a);
        bool valid=false;
        if (archive_read_open_filename(a,p.file.toUtf8().constData(),16384)==ARCHIVE_OK) {
            archive_entry *entry;
            while (archive_read_next_header(a,&entry)==ARCHIVE_OK) {
                if (strcmp(archive_entry_pathname(entry),"mimetype")==0) {
                    char data[64]{}; auto n=archive_read_data(a,data,sizeof(data));
                    valid=n==20 && memcmp(data,"application/epub+zip",20)==0; break;
                }
                archive_read_data_skip(a);
            }
        }
        archive_read_free(a);
        if (!valid) fail(EINVAL,"Invalid EPUB mimetype");
    }
}
}
extern "C" {
int rml_init(const char *directory) { return call([&] {
    root=QFileInfo(QString::fromUtf8(directory)).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) fail(ENOENT,"Library directory missing");
    rootBytes=root.toUtf8(); scan(); return 0;
}); }
const char *rml_root() { return rootBytes.constData(); }
const char *rml_error() { return lastError.constData(); }
int rml_list(const char *input,rml_list_cb cb,void *user) { return call([&] {
    auto p=path(input); scan(); QString parent;
    if (!p.isEmpty()) { auto e=resolve(p); if (!e.dir) fail(ENOTDIR,"Not a folder"); parent=e.id; }
    for (auto e: entries) if (e.parent==parent)
        cb(e.label.toUtf8().constData(),e.dir,e.dir?0:QFileInfo(file(e.id,'.'+e.ext)).size(),user);
    return 0;
}); }
int rml_stat(const char *input,struct stat *st) { return call([&] {
    auto p=path(input); memset(st,0,sizeof(*st));
    if (p.isEmpty()) { st->st_mode=S_IFDIR|0700; return 0; }
    if (pending.contains(p)) return ::stat(pending[p].file.toUtf8().constData(),st);
    auto e=resolve(p);
    if (::stat(file(e.id,e.dir?".metadata":'.'+e.ext).toUtf8().constData(),st)) fail(errno,"Cannot stat document");
    st->st_mode=(e.dir?S_IFDIR:S_IFREG)|0600; return 0;
}); }
int rml_open(const char *input,int flags,unsigned mode) { return call([&] {
    auto p=path(input);
    if (flags & (O_WRONLY|O_RDWR|O_CREAT|O_TRUNC)) {
        if (!(flags & O_CREAT)) fail(ENOTSUP,"In-place editing is not supported for library documents");
        if (!pending.contains(p)) {
            scan(); unused(p); QString ext=suffix(p), parent=parentFor(p);
            auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto name=QFileInfo(p).fileName().chopped(ext.size()+1);
            checkTitle(name);
            auto stage=root+"/.umtp-incoming";
            if (QFileInfo(stage).isSymLink() || !QDir().mkpath(stage)) fail(EIO,"Cannot create staging directory");
            pending.insert(p,{id,parent,name,ext,stage+'/'+id+".part"});
        }
        return ::open(pending[p].file.toUtf8().constData(),flags|O_NOFOLLOW|O_CLOEXEC,0600);
    }
    auto e=resolve(p); if (e.dir) fail(EISDIR,"Cannot read folder");
    return ::open(file(e.id,'.'+e.ext).toUtf8().constData(),flags|O_NOFOLLOW|O_CLOEXEC,mode);
}); }
int rml_finish(const char *input,int success) { return call([&] {
    auto p=path(input); if (!pending.contains(p)) return 0;
    auto item=pending.take(p);
    if (!success) { QFile::remove(item.file); return 0; }
    try {
        validate(item); scan(); unused(p);
        if (parentFor(p)!=item.parent) fail(EBUSY,"Destination folder changed");
        auto target=file(item.id,'.'+item.ext);
        if (!QFile::rename(item.file,target)) fail(EIO,"Cannot publish uploaded file");
        QJsonObject content{{"fileType",item.ext},{"extraMetadata",QJsonObject{}},{"fontName",""},
            {"lastOpenedPage",0},{"lineHeight",-1},{"margins",100},{"textScale",1},
            {"orientation","portrait"},{"pageCount",0},{"pages",QJsonArray{}},{"redirectionPageMap",QJsonArray{}},
            {"transform",QJsonObject{{"m11",1},{"m12",0},{"m13",0},{"m21",0},{"m22",1},{"m23",0},{"m31",0},{"m32",0},{"m33",1}}}};
        save(file(item.id,".content"),QJsonDocument(content).toJson());
        // Metadata is the publication point; payload/content are complete first.
        save(file(item.id,".metadata"),QJsonDocument(metadata(item.id,item.parent,item.name,false)).toJson());
        scan(); return 0;
    } catch (...) {
        QFile::remove(item.file);
        // These UUIDs were created by this upload; never remove existing documents.
        if (!QFileInfo::exists(file(item.id,".metadata"))) {
            QFile::remove(file(item.id,".content")); QFile::remove(file(item.id,'.'+item.ext));
        }
        throw;
    }
}); }
int rml_mkdir(const char *input) { return call([&] {
    auto p=path(input); if (p.isEmpty()) fail(EEXIST,"Root already exists"); scan(); unused(p);
    checkTitle(QFileInfo(p).fileName());
    auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    save(file(id,".metadata"),QJsonDocument(metadata(id,parentFor(p),QFileInfo(p).fileName(),true)).toJson());
    scan(); return 0;
}); }
int rml_rename(const char *from,const char *to) { return call([&] {
    auto p=path(from), q=path(to); if (p==q) return 0;
    scan(); auto e=resolve(p); unused(q); auto parent=parentFor(q);
    if (e.dir && q.startsWith(p+'/')) fail(EINVAL,"Cannot move folder into itself");
    auto name=QFileInfo(q).fileName();
    if (!e.dir) { if (suffix(q)!=e.ext) fail(EINVAL,"Cannot change book format by renaming"); name.chop(e.ext.size()+1); }
    if(QFileInfo(p).fileName()==QFileInfo(q).fileName()) name=e.name;
    else checkTitle(name);
    auto m=e.meta; m["visibleName"]=name; m["parent"]=parent; update(e,m); scan(); return 0;
}); }
int rml_remove(const char *input) { return call([&] {
    auto p=path(input); if (pending.contains(p)) return rml_finish(input,0);
    scan(); auto e=resolve(p); auto m=e.meta; m["parent"]="trash"; update(e,m); scan(); return 0;
}); }
void rml_abort_pending() { std::lock_guard<std::recursive_mutex> lock(mutex); for (auto p: pending) QFile::remove(p.file); pending.clear(); }
}
