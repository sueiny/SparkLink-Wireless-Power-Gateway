#include "common/file_utils.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace gateway::common {

std::string dirnameOf(const std::string &path)
{
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

std::string joinPath(const std::string &dir, const std::string &name)
{
    if (dir.empty() || dir == ".")
        return name;
    if (dir.back() == '/')
        return dir + name;
    return dir + "/" + name;
}

bool pathExists(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

bool regularFileExists(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

namespace {

bool mkdirOne(const std::string &path)
{
    if (path.empty() || pathExists(path))
        return true;

    if (::mkdir(path.c_str(), 0755) == 0)
        return true;

    return errno == EEXIST;
}

} // namespace

bool mkdirRecursive(const std::string &dir)
{
    if (dir.empty() || dir == ".")
        return true;

    std::string current;
    size_t index = 0;

    if (!dir.empty() && dir[0] == '/') {
        current = "/";
        index = 1;
    }

    while (index <= dir.size()) {
        const auto next = dir.find('/', index);
        const std::string part = dir.substr(index, next - index);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/')
                current += '/';
            current += part;
            if (!mkdirOne(current))
                return false;
        }

        if (next == std::string::npos)
            break;
        index = next + 1;
    }

    return true;
}

bool ensureParentDir(const std::string &path)
{
    return mkdirRecursive(dirnameOf(path));
}

bool readText(const std::string &path, std::string *out)
{
    if (!out)
        return false;

    std::ifstream in(path);
    if (!in)
        return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool writeTextAtomic(const std::string &path, const std::string &text)
{
    if (!ensureParentDir(path))
        return false;

    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out)
            return false;
        out << text;
        out.flush();
        if (!out)
            return false;
    }

    const int fd = ::open(tmp_path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    const bool file_synced = ::fsync(fd) == 0;
    ::close(fd);
    if (!file_synced)
        return false;

    if (::rename(tmp_path.c_str(), path.c_str()) != 0)
        return false;

    const std::string parent = dirnameOf(path);
    const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
    return true;
}

bool renameIfExists(const std::string &from, const std::string &to)
{
    if (!pathExists(from))
        return true;
    if (pathExists(to))
        ::unlink(to.c_str());
    return ::rename(from.c_str(), to.c_str()) == 0;
}

bool unlinkIfExists(const std::string &path)
{
    if (!pathExists(path))
        return true;
    return ::unlink(path.c_str()) == 0;
}

bool copyFile(const std::string &from, const std::string &to)
{
    if (!ensureParentDir(to))
        return false;

    std::ifstream in(from, std::ios::binary);
    if (!in)
        return false;

    const std::string tmp_path = to + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << in.rdbuf();
        out.flush();
        if (!out)
            return false;
    }

    const int fd = ::open(tmp_path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    const bool file_synced = ::fsync(fd) == 0;
    ::close(fd);
    if (!file_synced)
        return false;

    if (::rename(tmp_path.c_str(), to.c_str()) != 0)
        return false;

    const int dir_fd = ::open(dirnameOf(to).c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
    return true;
}

} // namespace gateway::common
