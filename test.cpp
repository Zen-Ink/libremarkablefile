#include "remarkable.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
static QByteArray pdf="%PDF-1.4\n1 0 obj << /Type /Catalog >> endobj\n%%EOF\n";
static void upload(const char *name,const QByteArray &data,bool valid=true) {
    int fd=rml_open(name,O_CREAT|O_WRONLY|O_TRUNC,0600); assert(fd>=0);
    assert(write(fd,data.data(),data.size())==data.size()); close(fd);
    int result=rml_finish(name,1); assert(valid ? result==0 : result<0);
}
int main() {
    QTemporaryDir dir; assert(dir.isValid()); assert(rml_init(dir.path().toUtf8())==0);
    assert(rml_mkdir("书籍")==0); upload("书籍/测试.pdf",pdf);
    assert(rml_mkdir("x")==0); upload("x/UPPER.PDF",pdf);
    assert(rml_mkdir("a")==0); assert(rml_remove("x/UPPER.pdf")==0);
    assert(rml_remove("x")==0); assert(rml_remove("a")==0);
    struct stat st{}; assert(rml_stat("书籍/测试.pdf",&st)==0 && st.st_size==pdf.size());
    int fd=rml_open("书籍/测试.pdf",O_RDONLY,0); assert(fd>=0);
    char buffer[1024]; assert(read(fd,buffer,sizeof buffer)==pdf.size()); close(fd);
    assert(QByteArray(buffer,pdf.size())==pdf);
    assert(rml_open("书籍/测试.pdf",O_CREAT|O_WRONLY,0600)<0 && errno==EEXIST);
    assert(rml_open("bad.txt",O_CREAT|O_WRONLY,0600)<0 && errno==ENOTSUP);
    assert(rml_open("../bad.pdf",O_CREAT|O_WRONLY,0600)<0 && errno==EINVAL);
    upload("bad.pdf","invalid",false); assert(rml_stat("bad.pdf",&st)<0);
    // Preserve firmware fields when moving/renaming an existing document.
    QString book;
    for (auto name:QDir(dir.path()).entryList({"*.metadata"})) {
        QFile f(dir.path()+'/'+name); assert(f.open(QIODevice::ReadOnly));
        auto obj=QJsonDocument::fromJson(f.readAll()).object(); f.close();
        if (obj["type"]!="DocumentType" || obj["visibleName"]!="测试") continue;
        book=f.fileName(); obj["futureFirmwareField"]=QJsonObject{{"keep",42}};
        assert(f.open(QIODevice::WriteOnly)); f.write(QJsonDocument(obj).toJson());
    }
    assert(rml_rename("书籍/测试.pdf","renamed.pdf")==0);
    QFile f(book); assert(f.open(QIODevice::ReadOnly));
    auto obj=QJsonDocument::fromJson(f.readAll()).object();
    assert(obj["futureFirmwareField"].toObject()["keep"].toInt()==42);
    assert(obj["version"].toInt()==2); assert(obj["parent"]=="");
    assert(rml_remove("renamed.pdf")==0); assert(rml_stat("renamed.pdf",&st)<0);
    assert(rml_remove("书籍")==0);
    int count=0; assert(rml_list("",[](const char*,int,int64_t,void*p){++*(int*)p;},&count)==0); assert(count==0);
    puts("PASS direct library import/read/rename/move/trash, validation and unknown-field preservation");
}
