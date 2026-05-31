#pragma once

#include <string>

namespace gateway::common {

std::string dirnameOf(const std::string &path);
std::string joinPath(const std::string &dir, const std::string &name);
bool pathExists(const std::string &path);
bool regularFileExists(const std::string &path);
bool mkdirRecursive(const std::string &dir);
bool ensureParentDir(const std::string &path);
bool readText(const std::string &path, std::string *out);
bool writeTextAtomic(const std::string &path, const std::string &text);
bool renameIfExists(const std::string &from, const std::string &to);
bool unlinkIfExists(const std::string &path);
bool copyFile(const std::string &from, const std::string &to);

} // namespace gateway::common
