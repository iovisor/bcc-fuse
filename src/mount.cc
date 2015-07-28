/*
 * Copyright (c) 2015 PLUMgrid, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstring>
#include <fuse.h>
#include <string>
#include <vector>

#include "mount.h"
#include "string_util.h"

namespace bcc {

using std::find;
using std::string;
using std::vector;

Mount::Mount() : flags_(0) {
  log_ = fopen("/tmp/bcc-fuse.log", "w");
  oper_.reset(new fuse_operations);
  root_.reset(new RootDir(this, 0755));
  memset(&*oper_, 0, sizeof(*oper_));
  oper_->getattr = getattr_;
  oper_->readdir = readdir_;
  oper_->mkdir = mkdir_;
  oper_->open = open_;
  oper_->read = read_;
  oper_->write = write_;
  oper_->truncate = truncate_;
  oper_->flush = flush_;
  oper_->readlink = readlink_;
}

Mount::~Mount() {
  fclose(log_);
}

Mount * Mount::instance() {
  return static_cast<Mount *>(fuse_get_context()->private_data);
}

vector<string> Mount::props_ = {
  "source",
  "valid",
};
vector<string> Mount::subdirs_ = {
  "maps",
};

int Mount::getattr(const char *path, struct stat *st) {
  log("getattr: %s\n", path);
  memset(st, 0, sizeof(*st));
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf || p.next())
    return -ENOENT;
  return leaf->getattr(st);
}

int Mount::readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                   struct fuse_file_info *fi) {
  log("readdir: %s\n", path);
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::dir_e)
    return static_cast<Dir *>(leaf)->readdir(buf, filler, offset, fi);
  return -EBADF;
}

int Mount::mkdir(const char *path, mode_t mode) {
  log("mkdir: %s\n", path);
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  p.consume();
  if (!p.cur())
    return -EEXIST;
  if (p.next())
    return -ENOENT;
  if (leaf->type() == Inode::dir_e)
    return static_cast<Dir *>(leaf)->mkdir(p.cur(), mode);
  return -ENOTDIR;
}

int Mount::open(const char *path, struct fuse_file_info *fi) {
  log("open: %s\n", path);
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::dir_e)
    return -EISDIR;
  return static_cast<File *>(leaf)->open(fi);
}

int Mount::read(const char *path, char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi) {
  log("read: %s sz=%zu off=%zu\n", path, size, offset);
  Path p(path);
  Inode *leaf = fi->fh ? (Inode *)fi->fh : root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::file_e)
    return static_cast<File *>(leaf)->read(buf, size, offset, fi);
  return -EISDIR;
}

int Mount::write(const char *path, const char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) {
  log("write: %s sz=%zu off=%zu\n", path, size, offset);
  Path p(path);
  Inode *leaf = fi->fh ? (Inode *)fi->fh : root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::file_e)
    return static_cast<File *>(leaf)->write(buf, size, offset, fi);
  return -EISDIR;
}

int Mount::truncate(const char *path, off_t newsize) {
  log("truncate: %s sz=%zd\n", path, newsize);
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::file_e)
    return static_cast<File *>(leaf)->truncate(newsize);
  return -EISDIR;
}

int Mount::flush(const char *path, struct fuse_file_info *fi) {
  log("flush: %s\n", path);
  Path p(path);
  Inode *leaf = fi->fh ? (Inode *)fi->fh : root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::file_e)
    return static_cast<File *>(leaf)->flush(fi);
  return -EISDIR;
}

int Mount::readlink(const char *path, char *buf, size_t size) {
  log("readlink: %s\n", path);
  Path p(path);
  Inode *leaf = root_->leaf(&p);
  if (!leaf)
    return -ENOENT;
  if (leaf->type() == Inode::link_e)
    return static_cast<Link *>(leaf)->readlink(buf, size);
  return -EINVAL;
}

int Mount::run(int argc, char **argv) {
  return fuse_main(argc, argv, &*oper_, this);
}

}  // namespace bcc