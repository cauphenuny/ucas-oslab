#include <logger.h>
#include <os/errno.h>
#include <os/fs.h>
#include <os/string.h>

static int filename_cmp(const char* path1, const char* path2) {
    return strncmp(path1, path2, MAX_FILE_NAME);
}

static char* filename_cpy(char* dest, const char* src) { return strncpy(dest, src, MAX_FILE_NAME); }

inode_t* dir_lookup(inode_t* dir, const char* filename, size_t* poff) {
    asserts(dir != NULL, "dir_lookup with NULL dir");
    asserts(filename != NULL, "dir_lookup with NULL filename");
    asserts(dir->type == FS_TYPE_DIR, "dir_lookup on non-directory inode");

    for (size_t offset = 0; offset < dir->size; offset += sizeof(dentry_t)) {
        dentry_t dentry;
        int n = inode_read(dir, &dentry, 0, offset, sizeof(dentry_t));
        asserts(n == sizeof(dentry_t), "short read");
        if (dentry.inode_num == 0) continue;
        if (filename_cmp(dentry.name, filename) == 0) {
            if (poff) *poff = offset;
            return inode_ref(dentry.inode_num);
        }
    }
    if (poff) *poff = 0;
    return NULL;
}

// NOTE: do not change link count
int dir_link(inode_t* dir, const char* filename, int inode_num) {
    asserts(dir != NULL, "dir_link with NULL dir");
    asserts(filename != NULL, "dir_link with NULL filename");
    asserts(dir->type == FS_TYPE_DIR, "dir_link on non-directory inode");

    inode_t* child = dir_lookup(dir, filename, NULL);
    if (child != NULL) {
        inode_deref(child);
        return ERR_FILE_EXISTS;
    }

    size_t offset = 0;
    for (offset = 0; offset < dir->size; offset += sizeof(dentry_t)) {
        dentry_t dentry;
        int n = inode_read(dir, &dentry, 0, offset, sizeof(dentry_t));
        asserts(n == sizeof(dentry_t), "short read");
        if (dentry.inode_num == 0) {
            pretty_logd("find invalid entry at offset %d, reuse it", offset);
            break;
        }
    }

    dentry_t dentry;
    memset(&dentry, 0, sizeof(dentry_t));
    filename_cpy(dentry.name, filename);
    dentry.inode_num = inode_num;

    int n = inode_write(dir, &dentry, 0, offset, sizeof(dentry_t));
    if (n != sizeof(dentry_t)) {
        pretty_logw("failed to write directory entry");
        return ERR_OPERATION_FAILED;
    }
    return 0;
}

// NOTE: changes link count
int dir_unlink(inode_t* dir, const char* filename) {
    asserts(dir != NULL, "dir_unlink with NULL dir");
    asserts(filename != NULL, "dir_unlink with NULL filename");
    asserts(dir->type == FS_TYPE_DIR, "dir_unlink on non-directory inode");

    size_t offset;
    inode_t* child = dir_lookup(dir, filename, &offset);
    if (child == NULL) {
        return ERR_FILE_NOT_EXISTS;
    }

    inode_open(child);
    if (child->type == FS_TYPE_DIR) {
        inode_close(child);
        return ERR_FILE_TYPE_MISMATCH;
    }

    child->link_count--;

    dentry_t dentry;
    memset(&dentry, 0, sizeof(dentry_t));
    int n = inode_write(dir, &dentry, 0, offset, sizeof(dentry_t));
    if (n != sizeof(dentry_t)) {
        inode_close(child);
        return ERR_OPERATION_FAILED;
    }

    inode_close(child);
    return 0;
}

int dir_isempty(inode_t* dir) {
    // check if empty
    for (size_t off = sizeof(dentry_t) * 2; off < dir->size; off += sizeof(dentry_t)) {
        dentry_t dentry;
        int n = inode_read(dir, &dentry, 0, off, sizeof(dentry_t));
        asserts(n == sizeof(dentry_t), "short read");
        if (dentry.inode_num != 0) {
            return 0;  // not empty
        }
    }
    return 1;
}

int dir_rmdir(inode_t* dir, const char* dirname) {
    asserts(dir != NULL, "dir_rmdir with NULL dir");
    asserts(dirname != NULL, "dir_rmdir with NULL dirname");
    asserts(dir->type == FS_TYPE_DIR, "dir_rmdir on non-directory inode");

    if (filename_cmp(dirname, ".") == 0 || filename_cmp(dirname, "..") == 0) {
        pretty_logd("cannot remove . or .. directory");
        return ERR_FILE_NO_PERMISSION;
    }

    size_t offset;
    inode_t* child = dir_lookup(dir, dirname, &offset);

    if (child == NULL) {
        return ERR_FILE_NOT_EXISTS;
    }

    inode_open(child);

    if (child->type != FS_TYPE_DIR) {
        inode_close(child);
        pretty_logd("%s is not a directory", dirname);
        return ERR_FILE_TYPE_MISMATCH;
    }

    if (!dir_isempty(child)) {
        inode_close(child);
        pretty_logd("directory %s not empty", dirname);
        return ERR_DIRECTORY_NOT_EMPTY;  // not empty
    }

    dentry_t dentry;
    memset(&dentry, 0, sizeof(dentry_t));
    int n = inode_write(dir, &dentry, 0, offset, sizeof(dentry_t));
    if (n != sizeof(dentry_t)) {
        pretty_logw("failed to remove directory entry %s", dirname);
        return ERR_OPERATION_FAILED;
    }

    if (child->type == FS_TYPE_DIR) {
        dir->link_count--;
    }

    child->link_count--;
    inode_close(child), child = NULL;

    return 0;
}

// Examples:
//   pathshift("a/bb/c", name) = "bb/c", setting name = "a"
//   pathshift("///a//bb", name) = "bb", setting name = "a"
//   pathshift("a", name) = "", setting name = "a"
//   pathshift("", name) = pathshift("////", name) = 0

const char* path_shift(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/') path++;
    if (*path == 0) return 0;
    s = path;
    while (*path != '/' && *path != 0) path++;
    len = path - s;
    if (len >= MAX_FILE_NAME)
        memcpy(name, s, MAX_FILE_NAME);
    else {
        memcpy(name, s, len);
        name[len] = 0;
    }
    while (*path == '/') path++;
    return path;
}

inode_t* path_resolve(const char* path, bool skip_last, char* name) {
    inode_t *cur, *next;
    if (*path == '/')
        cur = inode_ref(superblock.root_inode);
    else
        cur = inode_ref(current_running->cwd_inode);

    if (*path == 0) {
        pretty_logd("resolving empty path, return current inode: %d", cur->inode_num);
        *name = 0;
        return cur;
    }

    pretty_logi("current inode: %d", cur->inode_num);

    while ((path = path_shift(path, name)) != 0) {
        pretty_logi("resolving path component: %s", name);
        inode_open(cur);
        if (cur->type != FS_TYPE_DIR) {
            pretty_logw("failed: not a directory");
            inode_close(cur);
            return NULL;
        }
        if (skip_last && *path == 0) {
            pretty_logi("skip last component, return parent inode: %d", cur->inode_num);
            inode_unlock(cur);
            return cur;
        }
        next = dir_lookup(cur, name, NULL);
        if (next == NULL) {
            pretty_logw("failed: not such file or directory");
            inode_close(cur);
            return NULL;
        }
        inode_close(cur);
        cur = next;
        pretty_logi("next inode: %d", cur->inode_num);
    }

    if (skip_last) {  // no parent
        inode_deref(cur);
        return NULL;
    }

    return cur;
}

inode_t* path_resolve_entry(const char* path) {
    char name[MAX_FILE_NAME];
    return path_resolve(path, false, name);
}

inode_t* path_resolve_parent(const char* path, char* name) {
    return path_resolve(path, true, name);
}

inode_t* path_create(const char* path, int type) {
    char name[MAX_FILE_NAME];
    inode_t* parent = path_resolve_parent(path, name);
    if (parent == NULL) {
        return NULL;
    }

    inode_open(parent);
    inode_t* child = dir_lookup(parent, name, NULL);
    if (child != NULL) {
        inode_open(child);
        if (child->type != type) {
            inode_close(child);
            inode_close(parent);
            child = NULL;
        } else {
            inode_unlock(child);
            inode_close(parent);
        }
        return child;
    }

    child = inode_alloc(type);
    if (child == NULL) {
        inode_deref(parent);
        return NULL;
    }

    inode_open(child);
    child->link_count = 1;

    if (type == FS_TYPE_DIR) {
        if (dir_link(child, ".", child->inode_num) != 0 ||
            dir_link(child, "..", parent->inode_num) != 0) {
            goto fail;
        }
    }

    if (dir_link(parent, name, child->inode_num) != 0) {
        goto fail;
    }

    if (type == FS_TYPE_DIR) {
        parent->link_count++;
    }

    inode_close(parent);
    inode_unlock(child);
    return child;

fail:
    child->link_count = 0;
    inode_close(child);
    inode_close(parent);
    return NULL;
}

int path_remove(const char* path, int isdir) {
    char name[MAX_FILE_NAME];
    inode_t* parent = path_resolve_parent(path, name);
    if (parent == NULL) {
        return ERR_FILE_NOT_EXISTS;
    }
    pretty_logi("parsed path '%s', parent %d, name '%s'", path, parent->inode_num, name);

    inode_open(parent);
    int ret = isdir ? dir_rmdir(parent, name) : dir_unlink(parent, name);
    inode_close(parent);

    return ret;
}
