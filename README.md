# libremarkable

直接读写 reMarkable 书籍库的 C ABI 静态库，内部使用 Qt 6 Core 和 libarchive；不依赖 xochitl 进程或 rm-librarian。

参考 `nfj/core.remarkable.nfj` 的 Metadata、ContentV1/V2 字段规则，独立实现。以 UUID 和 `parent` 重建文件夹树，只暴露 PDF / EPUB 原始文件；不导出手写笔记、批注或渲染结果。读取 V1/V2 的共同 `fileType`，修改既有文件时保留未知 JSON 字段和原有 `.content`。

```sh
make
make check
# ARMv7 SDK 环境中：
make BUILD=build-armv7
```

使用 `remarkable.h`：`rml_init(书库物理路径)`，路径参数均为相对展示路径；`rml_list` 枚举，`rml_open` 返回 POSIX fd。上传用 `O_CREAT|O_WRONLY`，写完 fsync/close 后必须调用 `rml_finish(path, 1)`；取消用 `rml_finish(path, 0)`，会话结束用 `rml_abort_pending()`。失败返回 -1，详细原因通过 errno / `rml_error()` 获取。一个进程只管理一个书库。

上传先写 `.umtp-incoming`，PDF 检查首尾标记，EPUB 检查 ZIP mimetype；这不是完整的文档语法验证。发布顺序是原文件、`.content`、`.metadata`，写元数据使用原子替换。拒绝覆盖现有书籍，避免破坏批注和页面映射。进程被 SIGKILL 或断电时，暂存文件可能保留，且不会出现在书库中。

创建默认 V1 内容；新书页面数初始为 0，交给 xochitl 导入/打开时处理。改名、移动保留 UUID，修改版本与时间戳；删除只把元数据的 `parent` 改为 `trash`。文件夹删除也保留其子项数据。

重名条目的展示名带完整 UUID；不适合文件名的原有标题进行转义，过长标题截断并附 UUID。新建/改名要求普通文件名字符，标题最多 170 个 UTF-8 字节；PDF/EPUB 扩展名不区分大小写。库以目录操作为界重新扫描元数据，大书库会较慢。

并发修改使用写入前内容比较检测冲突；无法与 xochitl/cloud 建立跨进程原子事务。修改书库时应回到首页并避免同时在平板或云端修改同一条目。没有实时刷新时，xochitl 可能需要重新加载/重启才显示变化。
