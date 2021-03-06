#include "./Utils.h"
#include <algorithm>
#include <thread>
#include <sstream>

#if defined(BR2_CPP17)
#include <filesystem>
#endif

#if defined(BR2_OS_LINUX)
//This gets the OS name
#include <sys/utsname.h>
//Sort of similar to windows GetLastError
#include <sys/errno.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <system_error>
#endif

#ifdef BR2_OS_WINDOWS
#include "./dirent.h"
#endif

namespace SCP {

#pragma region Stopwatch

void Stopwatch::start() {
  start_t = std::chrono::high_resolution_clock::now();
}
void Stopwatch::end() {
  end_t = std::chrono::high_resolution_clock::now();
}
string_t Stopwatch::time() {
  auto diff_t = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t);
  double span_t = diff_t.count();

  string_t unit = "us";
  //Auto convert from us -> ms -> s based on sigfigs
  if (span_t > 1000) {
    span_t /= 1000.0;
    unit = "ms";
    if (span_t > 1000) {
      span_t /= 1000.0;
      unit = "s";
      if (span_t > 60) {
        span_t /= 60.0;
        unit = "m";
        if (span_t > 60) {
          span_t /= 60.0;
          unit = "h";
          if (span_t > 24) {
            span_t /= 24.0;
            unit = "d";
          }
        }
      }
    }
  }

  std::ostringstream outss;
  int precision = 1;
  outss.precision((std::streamsize)1);
  outss << std::fixed << span_t;
  string_t end_t_sz = outss.str();
  string_t out = Stz end_t_sz + unit;
  return out;
}

#pragma endregion

#pragma region Utils

std::thread::id Utils::_mainThreadId;  //There is no ctor for thread::id
std::string Utils::_exeLocation = "";

void Utils::throwException(const string_t& ex) {
  string_t st = Stz + "Exception: " + ex;
  throw std::invalid_argument(st.c_str());
}

string_t Utils::getDate() {
  time_t rawtime;
  struct tm* timeinfo;
  char buffer[80];

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer, sizeof(buffer), "%d%m%Y %H:%M:%S", timeinfo);
  string_t ret = string_t(buffer);

  return ret;
}
void Utils::log(const string_t& s, std::vector<OutputLine>* out, ColoredConsole::Color color) {
  if (out == nullptr) {
    if (std::this_thread::get_id() != Utils::_mainThreadId) {
      Utils::throwException("Attempted to call log from a shader task without specifying output.");
    }
    ColoredConsole::print(s, color);
  }
  else {
    out->push_back(OutputLine{ s, color });
  }
}
void Utils::logError(const string_t& s, std::vector<OutputLine>* out, bool color) {
  std::string sb = "[" + Utils::getDate() + "][E]:" + s + "\n";
  Utils::log(sb, out, color ? ColoredConsole::Color::FG_RED : ColoredConsole::Color::FG_GRAY);
}
void Utils::logWarn(const string_t& s, std::vector<OutputLine>* out, bool color) {
  std::string sb = "[" + Utils::getDate() + "][W]:" + s + "\n";
  Utils::log(sb, out, color ? ColoredConsole::Color::FG_YELLOW : ColoredConsole::Color::FG_GRAY);
}
void Utils::logInfo(const string_t& s, std::vector<OutputLine>* out, bool color) {
  std::string sb = "[" + Utils::getDate() + "][I]:" + s + "\n";
  Utils::log(sb, out, color ? ColoredConsole::Color::FG_CYAN : ColoredConsole::Color::FG_GRAY);
}
bool Utils::equals(const string_t& a, const string_t& b) {
  bool br = (a.compare(b) == 0);
  return br;
}
bool Utils::beginsWith(const string_t& search_str, const string_t& prefix) {
  bool ret = (search_str.rfind(prefix, 0) == 0);
  return ret;
}
void Utils::debugBreak() {
#if defined(BR2_OS_WINDOWS)
  DebugBreak();
#elif defined(BR2_OS_LINUX)
  raise(SIGTRAP);
#else
  OS_NOT_SUPPORTED_ERROR
#endif
}

string_t Utils::stripQuotes(const string_t& s) {
  string_t ret = s;
  ret.erase(std::remove(ret.begin(), ret.end(), '\"'), ret.end());
  ret.erase(std::remove(ret.begin(), ret.end(), '\''), ret.end());
  return ret;
}

bool Utils::fileExists(const string_t& filename) {
  //**NOTE: a portable option would be to use this code from SDL.
  // However the code below is supposed to be "faster"
  /*
  SDL_RWops *rw = SDL_RWFromFile(filename.c_str(), "rb");
  if (rw == nullptr)
  return false;

  SDL_RWclose(rw);
  return true;
  */

  //**Alternate version
  bool exists;
  struct stat buffer;
  exists = (stat(filename.c_str(), &buffer) == 0);

#if defined(BR2_OS_WINDOWS)
  // We get errors when calling stat in Windows for some reason.
  SetLastError(0);
#endif

  return exists;
}
bool Utils::directoryExists(const string_t& dirName) {
  bool ret;
  DIR* dir = NULL;

  dir = opendir(dirName.c_str());

  if (dir == NULL) {
    ret = false;
  }
  else {
    ret = true;
  }

  closedir(dir);

  return ret;
}
string_t Utils::executeReadOutput(const string_t& cmd) {
  string_t data = "";
#if defined(BR2_OS_LINUX)
#define POPEN_OS popen
#define PCLOSE_OS pclose
#elif defined(BR2_OS_WINDOWS)
#define POPEN_OS _popen
#define PCLOSE_OS _pclose
#endif

  //#if defined(BR2_OS_LINUX)
  //This works only if VSCode launches the proper terminal (some voodoo back there);
  const int MAX_BUFFER = 256;
  char buffer[MAX_BUFFER];
  memset(buffer, 0, MAX_BUFFER);
  string_t cmd_mod = Stz cmd + " 2>&1";  //redirect stderr to stdout

  FILE* stream = POPEN_OS(cmd_mod.c_str(), "r");
  if (stream) {
    while (fgets(buffer, MAX_BUFFER, stream) != NULL) {
      data.append(buffer);
    }
    if (ferror(stream)) {
      std::cout << "Error executeReadOutput() " << std::endl;
    }
    clearerr(stream);
    PCLOSE_OS(stream);
  }
  //#else
  //  int r = system(cmd.c_str());
  //
  //      std::array<char, 128> buffer;
  //  std::string result;
  //  std::unique_ptr<FILE, decltype(&pclose)> pipe(_popen(cmd, "r"), pclose);
  //  if (!pipe) {
  //    throw std::runtime_error("popen() failed!");
  //  }
  //  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
  //    result += buffer.data();
  //  }
  //  return result;
  //
  //#endif
  return data;
}
bool Utils::createDirectoryRecursive(const string_t& dirName) {
  bool bRet = true;
#if defined(BR2_CPP17)
  //Note: this creates directories with full permissions.
  std::error_code code;
  bool fs_ret = std::filesystem::create_directories(dirName, code);
  int a = code.value();
  int b = code.default_error_condition().value();
  if (a != b) {
    bRet = false;
    Utils::logError(code.message());
  }
  else {
    bRet = true;
  }
#else
  string_t dirCpy;
  dirCpy.assign(dirName);
  dirCpy = formatPath(dirCpy);

  if (directoryExists(dirName) == true) {
    return true;
  }

  std::vector<string_t> dirs = StringUtil::split(dirCpy, '/');
  string_t strDir = dirs[0];
  for (size_t i = 1; i < dirs.size(); ++i) {
    strDir = FileSystem::combinePath(strDir, dirs[i]);
    bRet = bRet && FileSystem::createDirectorySingle(strDir);
  }

#endif
  return bRet;
}
bool Utils::isFile(const string_t& fileOrDirPath) {
  // https://stackoverflow.com/questions/146924/how-can-i-tell-if-a-given-path-is-a-directory-or-a-file-c-c
  struct stat s;
  if (stat(fileOrDirPath.c_str(), &s) == 0) {
    if (s.st_mode & S_IFREG) {
      return true;
    }
  }
  else {
    // DO NOT LOG HERE
    // BroLogError("stat - failed");
  }
  return false;
}
bool Utils::isDir(const string_t& fileOrDirPath) {
  // https://stackoverflow.com/questions/146924/how-can-i-tell-if-a-given-path-is-a-directory-or-a-file-c-c
  struct stat s;
  if (stat(fileOrDirPath.c_str(), &s) == 0) {
    if (s.st_mode & S_IFDIR) {
      return true;
    }
  }
  else {
    // DO NOT LOG HERE
    // BroLogError("stat - failed");
  }
  return false;
}
bool Utils::getAllFilesOrDirs(const string_t& dir, std::vector<string_t>& dirs, bool bFiles) {
  dirs.resize(0);

  dirent* ep;
  DIR* dp;
  dp = opendir(dir.c_str());

  if (dp != NULL) {
    while ((ep = readdir(dp))) {
      string_t name(ep->d_name);
      if (!Utils::equals(name, "..") &&
          !Utils::equals(name, ".")) {
        string_t nameFull = Utils::combinePath(dir, name);
        if (!bFiles && Utils::isDir(nameFull)) {
          dirs.push_back(nameFull);
        }
        else if (bFiles && Utils::isFile(nameFull)) {
          dirs.push_back(nameFull);
        }
      }
    }
    closedir(dp);
  }
  else {
    Utils::logError("Couldn't open the directory '" + dir + "'");
    return false;
  }
  return true;
}
bool Utils::getAllDirs(const string_t& dir, std::vector<string_t>& dirs) {
  return getAllFilesOrDirs(dir, dirs, false);
}
bool Utils::getAllFiles(const string_t& dir, std::vector<string_t>& files) {
  return getAllFilesOrDirs(dir, files, true);
}
string_t Utils::combinePath(const std::string& a, const std::string& b) {
  //#if defined(BR2_CPP17)
  //  //Combining paths on Linux doesn't actually treat the RHS as a path or file if there's no /
  //  std::string bfmt;
  //  if (b.length() && ((b[0] != '\\') && (b[0] != '/'))) {
  //    bfmt = std::string("/") + b;
  //  }
  //  else {
  //    bfmt = b;
  //  }
  //  std::filesystem::path pa(a);
  //  std::filesystem::path pc = pa.concat(bfmt);
  //  string_t st = pc.string();
  //  return st;
  //#else
  if ((a.length() == 0) || (b.length() == 0)) {
    return a + b;
  }

  // - Make sure path is a / path not a \\ path
  auto a2 = formatPath(a);
  auto b2 = formatPath(b);

  // Remove / from before and after strings
  a2 = Utils::trim(a2, '/');
  b2 = Utils::trim(b2, '/');

  return a2 + string_t("/") + b2;
  //#endif
}
string_t Utils::getExtensionPartOfFileName(const string_t& name) {
  // 2017 12 22 - **Changed this to return the DOT just like .NET ( .exe, .dat
  // etc..)
  size_t x;
  string_t ext = "";
  string_t fileName;

  fileName = Utils::getFileNameFromPath(name);

  if ((x = fileName.rfind('.')) != string_t::npos) {
    ext = fileName.substr(x, fileName.length() - x);
  }

  return ext;
}
string_t Utils::getFilePartOfFileName(const string_t& fileName) {
  size_t off = fileName.rfind(".");

  if (off >= fileName.size() || off == string_t::npos) {
    Utils::throwException("Incorrect file name, no extension given or incorrect extension.");
  }

  return fileName.substr(0, off);
}
string_t Utils::getFileNameFromPath(const string_t& name) {
  //Returns the TLD for the path. Either filename, or directory.
  // Does not include the / in the directory.
  // ex ~/files/dir would return dir
  // - If formatPath is true then we'll convert the path to a / path.
  string_t fn = "";
#if defined(BR2_CPP17)
  fn = std::filesystem::path(name).filename().string();
#else

  DiskLoc l2;

  l2 = formatPath(name);

  //Index of TLD
  size_t tld_off = l2.rfind('/');

  if (tld_off != string_t::npos) {
    fn = l2.substr(tld_off + 1, l2.length() - tld_off + 1);
  }
  else {
    fn = name;
  }
#endif
  return fn;
}

string_t Utils::getDirectoryNameFromPath(const string_t& pathName) {
  // Returns the path without the filename OR top level directory.
  // See C# GetDirectoryName()
  string_t ret = "";

  string_t formattedPath;
  string_t tmpPath = pathName;
  string_t fn;

  formattedPath = formatPath(pathName);
  fn = getFileNameFromPath(formattedPath);
  size_t x = formattedPath.rfind('/');
  if (x != string_t::npos) {
    ret = formattedPath.substr(0, x);
  }
  else {
    ret = formattedPath;
  }
  return ret;
}
string_t Utils::formatPath(const string_t& p) {
  return Utils::replaceAll(p, '\\', '/');
}
string_t Utils::replaceAll(const string_t& str, char charToRemove, char charToAdd) {
  string_t::size_type x = 0;
  string_t ret = str;
  string_t ch;
  ch += charToAdd;
  while ((x = ret.find(charToRemove, x)) != string_t::npos) {
    ret.replace(x, 1, ch);
    x += ch.length();
  }
  return ret;
}
string_t Utils::replaceAll(const string_t& str, const string_t& strToRemove, const string_t& strToAdd) {
  string_t::size_type x = 0;
  string_t ret = str;
  while ((x = ret.find(strToRemove, x)) != string_t::npos) {
    ret.replace(x, strToRemove.length(), strToAdd);
    x += strToAdd.length();
  }
  return ret;
}
time_t Utils::getLastModifyTime(const string_t& location) {
  // Gets the last time the file was modified as a time_t
  struct stat fileInfo;

  if (Utils::fileExists(location) == false) {
    Utils::throwException("File '" + location + "' does not exist");
  }

  stat(location.c_str(), &fileInfo);
  return fileInfo.st_mtime;
}
void Utils::findFile(FileInfo& inf, const string_t& search_root) {
  if (inf._found) {
    return;
  }
  if (inf._file_without_path.length() == 0) {
    inf._file_without_path = Utils::getFileNameFromPath(inf._path);
  }
  std::vector<string_t> files;
  Utils::getAllFiles(search_root, files);
  for (auto& file : files) {
    auto file_no_path = Utils::getFileNameFromPath(file);
    if (Utils::equals(file_no_path, inf._file_without_path)) {
      inf._found = true;
      inf._modified = Utils::getLastModifyTime(file);
      inf._path = file;
      return;
    }
  }
  std::vector<string_t> dirs;
  Utils::getAllDirs(search_root, dirs);
  for (auto& dir : dirs) {
    findFile(inf, dir);
  }
}
void Utils::split(const string_t& in, const std::vector<char>& dels, std::vector<string_t>& ret) {
  string_t tbuf = "";
  //std::vector<t_string> ret;
  bool bDel;
  for (size_t n = 0; n < in.length(); ++n) {
    bDel = (bool)false;
    for (size_t idel = 0; idel < dels.size(); ++idel) {
      if ((in[n] == dels[idel]) && (tbuf.length() > 0)) {
        ret.push_back(tbuf);
        tbuf = "";
        bDel = true;
      }
    }
    if (bDel == (bool)false)
      tbuf += in[n];
  }
  if (tbuf.length()) {
    ret.push_back(tbuf);
  }
}
string_t Utils::trim(const string_t& astr, char trimch) {
  string_t str = astr;
  str = trimBeg(str, trimch);
  str = trimEnd(str, trimch);
  return str;
}
string_t Utils::trimBeg(const string_t& astr, char trimch) {
  string_t str = astr;
  while (str.length() && str.at(0) == trimch) {
    str = str.substr(1, str.length() - 1);
  }
  return str;
}
string_t Utils::trimEnd(const string_t& astr, char trimch) {
  string_t str = astr;
  while (str.length() && str.at(str.length() - 1) == trimch) {
    str = str.substr(0, str.length() - 1);
  }
  return str;
}
string_t Utils::enquote(const string_t& instr) {
  string_t outst = instr;
  outst = "\"" + outst + "\"";
  return outst;
}

#pragma endregion

}  // namespace SCP