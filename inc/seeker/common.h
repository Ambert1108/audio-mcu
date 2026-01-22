/**
@project seeker
@author Tao Zhang
@since 2020/3/1
@version 0.1.2-SNAPSHOT 2023/3/23
*/
#pragma once
#include <chrono>
#include <regex>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <map>
#include <random>

#include <climits>

#include <locale>   // std::wstring_convert
#include <codecvt>  // std::codecvt_utf8

namespace seeker {


class Time {
 public:
  static int64_t currentTime() {
    using namespace std::chrono;
    auto time_now = system_clock::now();
    auto durationIn = duration_cast<milliseconds>(time_now.time_since_epoch());
    return durationIn.count();
  };
};

namespace time {

inline int64_t currentTime() {
  using namespace std::chrono;
  auto time_now = system_clock::now();
  auto durationIn = duration_cast<milliseconds>(time_now.time_since_epoch());
  return durationIn.count();
};

// 2023年3月12日
inline std::chrono::system_clock::time_point timestamp2TimePoint(int64_t timestamp) {
  using namespace std::chrono;
  return system_clock::time_point(std::chrono::milliseconds(timestamp));
};

// 2023年3月12日
inline std::string toString(std::chrono::system_clock::time_point timepoint, const char* fmt) {
  using namespace std::chrono;
  std::time_t tt = std::chrono::system_clock::to_time_t(timepoint);
  std::tm tm = *std::localtime(&tt);
  auto tSeconds = std::chrono::duration_cast<std::chrono::seconds>(timepoint.time_since_epoch());
  std::stringstream ss;
  ss << std::put_time(&tm, fmt);
  auto tMilli = std::chrono::duration_cast<std::chrono::milliseconds>(timepoint.time_since_epoch());
  auto ms = tMilli - tSeconds;
  //ss << "." << std::setfill('0') << std::setw(3) << ms.count();
  return ss.str();
};

// 2023年3月12日
inline std::string toString(int64_t timepoint, const char* fmt = "%Y-%m-%d %H:%M:%S") {
  return toString(timestamp2TimePoint(timepoint), fmt);
};


}  // namespace time

class String {
 public:
  static std::string toLower(const std::string& target) {
    std::string out{};
    for (auto c : target) {
      out += ::tolower(c);
    }
    return out;
  }

  static std::string toUpper(const std::string& target) {
    std::string out{};
    for (auto c : target) {
      out += ::toupper(c);
    }
    return out;
  }

  static std::string trim(const std::string& s) {
    std::string rst{s};
    if (rst.empty()) {
      return rst;
    }
    rst.erase(0, rst.find_first_not_of(" "));
    rst.erase(s.find_last_not_of(" ") + 1);
    return rst;
  }

  static std::vector<std::string> split(const std::string& target, const std::string& sp) {
    std::vector<std::string> rst{};
    if (target.size() == 0) {
      return rst;
    }

    const auto spLen = sp.length();
    std::string::size_type pos = 0;
    auto f = target.find(sp, pos);

    while (f != std::string::npos) {
      auto r = target.substr(pos, f - pos);
      rst.emplace_back(r);
      pos = f + spLen;
      f = target.find(sp, pos);
    }
    rst.emplace_back(target.substr(pos, target.length()));
    return rst;
  }

  static std::string removeBlanks(const std::string& target) {
    static std::regex blankRe{R"(\s+)"};
    return std::regex_replace(target, blankRe, "");
  }

  static std::string removeLastEmptyLines(const std::string& target) {
    size_t len = target.length();
    size_t i = len - 1;
    for (; i > 0; i--) {
      if (target[i] == '\n') {
        continue;
      } else if (target[i] == '\r') {
        continue;
      } else {
        break;
      }
    }
    return target.substr(0, i + 1);
  }
};


// 2022年2月11日
namespace string {

inline std::string toLower(const std::string& target) {
  std::string out{};
  for (auto c : target) {
    out += ::tolower(c);
  }
  return out;
}

inline std::string toUpper(const std::string& target) {
  std::string out{};
  for (auto c : target) {
    out += ::toupper(c);
  }
  return out;
}

// 2022年2月12日
inline std::string trim(const std::string& target) {
  const size_t len = target.length();
  size_t begin = 0;

  for (; begin < len; begin++) {
    if (target[begin] == ' ' || target[begin] == '\t') {
      continue;
    } else {
      break;
    }
  }

  if (begin > len) {
    return "";
  }

  if (begin == len) {
    return target.substr(begin);
  }

  size_t end = len - 1;
  for (; end > begin; end--) {
    if (target[begin] == ' ' || target[begin] == '\t') {
      continue;
    } else {
      break;
    }
  }
  return target.substr(begin, end - begin + 1);
}

// inline std::vector<std::string> split(const std::string& target, const std::string& sp) {
//  std::vector<std::string> rst{};
//  if (target.size() == 0) {
//    return rst;
//  }
//
//  const auto spLen = sp.length();
//  std::string::size_type pos = 0;
//  auto f = target.find(sp, pos);
//
//  while (f != std::string::npos) {
//    auto r = target.substr(pos, f - pos);
//    rst.emplace_back(r);
//    pos = f + spLen;
//    f = target.find(sp, pos);
//  }
//  rst.emplace_back(target.substr(pos, target.length()));
//  return rst;
//}

inline std::vector<std::string> split(const std::string& target, const std::string& sp,
                                      uint32_t limit = UINT_MAX) {
  std::vector<std::string> rst{};
  if (target.size() == 0 || sp.size() == 0 || limit == 0 || limit == 1) {
    rst.push_back(target);
    return rst;
  }

  uint32_t count = 0;
  const auto spLen = sp.length();
  std::string::size_type pos = 0;
  auto f = target.find(sp, pos);

  while (f != std::string::npos && count < limit - 1) {
    auto r = target.substr(pos, f - pos);
    rst.emplace_back(r);
    count += 1;
    pos = f + spLen;
    f = target.find(sp, pos);
  }
  rst.emplace_back(target.substr(pos, target.length()));
  return rst;
}

inline std::string removeBlanks(const std::string& target) {
  static std::regex blankRe{R"(\s+)"};
  return std::regex_replace(target, blankRe, "");
}

inline std::string removeLastEmptyLines(const std::string& target) {
  const size_t len = target.length();
  size_t begin = 0;

  for (; begin < len; begin++) {
    if (target[begin] == '\r' || target[begin] == '\n') {
      continue;
    } else {
      break;
    }
  }

  if (begin > len) {
    return "";
  }

  if (begin == len) {
    return target.substr(begin);
  }

  size_t end = len - 1;
  for (; end > begin; end--) {
    if (target[end] == '\r' || target[end] == '\n') {
      continue;
    } else {
      break;
    }
  }
  return target.substr(begin, end - begin + 1);
}


// 2022年2月4日
inline bool startWith(const std::string& content, const std::string& target) {
  return content.size() >= target.size() && content.compare(0, target.size(), target) == 0;
}

// 2022年2月4日
inline bool endWith(const std::string& content, const std::string& target) {
  return content.size() >= target.size() &&
         content.compare(content.size() - target.size(), target.size(), target) == 0;
}

// 2022年2月11日
inline bool compareIgnoreCase(const std::string& content, const std::string& target) {
  return content.size() == target.size() && toLower(content).compare(toLower(target)) == 0;
}


// 2023年3月3日
/*
 * 正确分割中文字符
 */
class UTF8Converter {
 public:
  UTF8Converter() : converter{}, wString() {}

  void toCharVector(const std::string& utf8String, std::vector<std::string>& charVec) {
    wString = converter.from_bytes(utf8String);
    size_t charSize = wString.size();
    charVec.clear();
    charVec.reserve(charSize);
    const wchar_t* pos = wString.c_str();
    for (size_t i = 0; i < charSize; i++) {
      charVec.emplace_back(std::move(converter.to_bytes(pos + i, pos + i + 1)));
    }
  }

  std::vector<std::string> toCharVector(const std::string& utf8String) {
    std::vector<std::string> charVec;
    toCharVector(utf8String, charVec);
    return charVec;
  }

 private:
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
  std::wstring wString;
};


}  // namespace string

namespace file {

inline std::string readFileAsString(const std::string& filename) {
  std::string result;
  std::ifstream istream(filename, std::ios::binary);
  if (!istream.is_open()) {
    std::cout << "readFileAsString: Can not open file: " << filename << std::endl;
  } else {
    istream.seekg(0, std::ios::end);
    auto len = istream.tellg();
    if (len != -1) {  // 2022年10月30日
      result.reserve(len);
    }
    istream.seekg(0, std::ios::beg);
    result.assign((std::istreambuf_iterator<char>(istream)), std::istreambuf_iterator<char>());
  }
  return result;
}

// 2022年10月30日
inline size_t getVmRSS() {
  size_t vmRSS = 0;

#ifdef __linux__
  /*
VmPin:         0 kB
VmHWM:       864 kB
VmRSS:       864 kB
*/

  const static std::string regexString = R"regex(VmRSS:\s+(\d+)\s+kB)regex";
  const static std::regex reg(regexString);
  std::string str = seeker::file::readFileAsString("/proc/self/status");

  std::smatch sm;
  try {
    if (std::regex_search(str, sm, reg)) {
      vmRSS = std::atoi(sm[1].str().c_str());
    }
  } catch (std::exception& ex) {
    std::cout << "getVmRSS got exception: " << ex.what() << std::endl;
    vmRSS = 0;
  }

  return vmRSS;
#else
  // std::cout << "getVmRSS only support linux" << std::endl;
  return vmRSS;
#endif
}

}  // namespace file

class Map {
 public:
  template <typename K, typename V>
  inline static V getOrDefault(const std::unordered_map<K, V>& map, K key, V defaultValue) {
    auto search = map.find(key);
    if (search != map.end()) {
      return search->second;
    } else {
      return defaultValue;
    }
  }


  template <typename K, typename V>
  inline static V getOrDefault(const std::map<K, V>& map, K key, V defaultValue) {
    auto search = map.find(key);
    if (search != map.end()) {
      return search->second;
    } else {
      return defaultValue;
    }
  }
};

namespace bytes {
template <typename T>
inline void writeData(uint8_t* buf, T num, bool littleEndian = true) {
  size_t len(sizeof(T));
  if (littleEndian) {
    for (size_t i = 0; i < len; ++i) {
      buf[i] = (uint8_t)((num >> (i * 8)) & 0xFF);
    }
  } else {
    for (size_t i = 0; i < len; ++i) {
      buf[i] = (uint8_t)((num >> ((len - i - 1) * 8)) & 0xFF);
    }
  }
}

template <typename T>
inline void readData(const uint8_t* buf, T& num, bool littleEndian = true) {
  uint8_t len(sizeof(T));
  num = 0;
  if (littleEndian) {
    for (size_t i = 0; i < len; ++i) {
      num <<= 8;
      num |= (T)(buf[len - 1 - i] & 0xFF);
    }
  } else {  // not tested.
    for (size_t i = 0; i < len; ++i) {
      num <<= 8;
      num |= (T)(buf[i] & 0xFF);
    }
  }
}


inline void writeData(uint8_t* dst, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    dst[i] = *(data + i);
  }
}

inline void readData(uint8_t* src, uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    data[i] = *(src + i);
  }
}

inline void readData(uint8_t* src, std::vector<uint8_t>& data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    data.emplace_back(*(src + i));
  }
}

//// convert from hex to binary
inline std::vector<uint8_t> hex2bin(const std::string& hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("hex2bin: hex.size % 2 != 0");
  }
  std::vector<uint8_t> result;
  char c;
  for (size_t i = 0; i < hex.size(); i++) {
    c = std::tolower(hex[i]);
    uint8_t high = c >= 'a' ? hex[i] - 'a' + 10 : c - '0';
    i++;
    c = std::tolower(hex[i]);
    uint8_t low = c >= 'a' ? c - 'a' + 10 : c - '0';
    result.push_back(high * 16 + low);
  }
  return result;
}



inline std::string binary2HexString(const uint8_t* data, size_t len,
                                    const std::string& separate = " ", size_t lineLen = 0) {
  size_t hexInALine = 4096;
  if (lineLen > 0) {
    hexInALine = lineLen;
  }

  std::stringstream rst;
  rst << std::hex;
  for (size_t index = 0; index < len; index++) {
    int d = (int)*(data + index);
    rst << std::setw(2) << std::setfill('0') << (int)d << separate;
    if (index % hexInALine == hexInALine - 1) {
      rst << "\n";
    }
  }
  return rst.str();
}

inline std::string binary2HexString(const std::vector<uint8_t>& data, size_t len = 0,
                                    const std::string& separate = " ", size_t lineLen = 0) {
  size_t length = len == 0 || len > data.size() ? data.size() : len;
  return binary2HexString(data.data(), length, separate, lineLen);
}

template <typename T>
std::string number2Hex(T& num) {
  uint8_t len(sizeof(T));
  std::stringstream rst;
  rst << std::hex;
  for (size_t i = 0; i < len; ++i) {
    rst << std::setw(2) << std::setfill('0') << (int)((num >> ((len - 1 - i) * 8)) & 0xFF);
  }
  return rst.str();
}



inline void printBinary(const uint8_t* data, size_t len) {
  std::cout << std::hex;
  for (size_t index = 0; index < len; index++) {
    int d = (int)*(data + index);
    std::cout << std::setw(2) << std::setfill('0') << (int)d << " ";  // 005
    if (index % 16 == 15)
      printf(" | \n");
    else if (index % 8 == 7)
      printf(" | ");
    else if (index % 4 == 3)
      printf(" ");
  }
  std::cout << "\n";
  std::cout << std::dec;
}



inline void printBinary(const std::vector<uint8_t>& data, size_t len = 0) {
  size_t length = len == 0 || len > data.size() ? data.size() : len;
  printBinary(data.data(), length);
}



template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
inline std::vector<uint8_t> vectorToBinary(std::vector<T> src) {
  std::vector<uint8_t> out;

  int len = src.size();
  out.resize(sizeof(len) + sizeof(T) * len);

  uint8_t* pos = out.data();
  seeker::bytes::writeData(pos, len);
  pos += sizeof(len);

  for (T& e : src) {
    seeker::bytes::writeData<T>(pos, e, true);
    pos += sizeof(e);
  }

  return out;
}

template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
inline std::vector<T> binaryToVector(std::vector<uint8_t> src) {
  if (src.size() < 4) {
    throw std::runtime_error("deserialization: src.size() < 4");
  }

  std::vector<T> out;
  int len = src.size();

  uint8_t* pos = src.data();
  int outLen = 0;
  seeker::bytes::readData(pos, outLen);
  pos += sizeof(outLen);
  out.reserve(outLen);
  if (outLen * sizeof(T) + 4 > len) {
    throw std::runtime_error("deserialization: src.size() is too short for outLen.");
  }

  for (int i = 0; i < outLen; i++) {
    T element;
    seeker::bytes::readData(pos, element, true);
    out.push_back(element);
    pos += sizeof(T);
  }

  return out;
}


}  // namespace bytes

class ByteArray {
 public:
  template <typename T>
  inline static void writeData(uint8_t* buf, T num, bool littleEndian = true) {
    size_t len(sizeof(T));
    if (littleEndian) {
      for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)((num >> (i * 8)) & 0xFF);
      }
    } else {
      for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)((num >> ((len - i - 1) * 8)) & 0xFF);
      }
    }
  }

  template <typename T>
  inline static void readData(const uint8_t* buf, T& num, bool littleEndian = true) {
    uint8_t len(sizeof(T));
    num = 0;
    if (littleEndian) {
      for (size_t i = 0; i < len; ++i) {
        num <<= 8;
        num |= (T)(buf[len - 1 - i] & 0xFF);
      }
    } else {  // not tested.
      for (size_t i = 0; i < len; ++i) {
        num <<= 8;
        num |= (T)(buf[i] & 0xFF);
      }
    }
  }


  static void writeData(uint8_t* dst, uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      dst[i] = *(data + i);
    }
  }

  static void readData(uint8_t* src, uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      data[i] = *(src + i);
    }
  }

  static void readData(uint8_t* src, std::vector<uint8_t>& data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      data.emplace_back(*(src + i));
    }
  }

  //// convert from hex to binary
  static std::vector<uint8_t> hex2bin(const std::string& hex) {
    if (hex.size() % 2 != 0) {
      throw std::runtime_error("hex2bin: hex.size % 2 != 0");
    }
    std::vector<uint8_t> result;
    char c;
    for (size_t i = 0; i < hex.size(); i++) {
      c = std::tolower(hex[i]);
      uint8_t high = c >= 'a' ? hex[i] - 'a' + 10 : c - '0';
      i++;
      c = std::tolower(hex[i]);
      uint8_t low = c >= 'a' ? c - 'a' + 10 : c - '0';
      result.push_back(high * 16 + low);
    }
    return result;
  }

  static void printBinary(const uint8_t* data, size_t len) {
    std::cout << std::hex;
    for (size_t index = 0; index < len; index++) {
      int d = (int)*(data + index);
      std::cout << std::setw(2) << std::setfill('0') << (int)d << " ";  // 005
      if (index % 16 == 15)
        printf(" | \n");
      else if (index % 8 == 7)
        printf(" | ");
      else if (index % 4 == 3)
        printf(" ");
    }
    std::cout << "\n";
    std::cout << std::dec;
  }

  static void printBinary(const std::vector<uint8_t>& msg, size_t len = INT_MAX) {
    std::cout << std::hex;
    size_t index = 0;
    for (const uint8_t& d : msg) {
      if (index >= len) {
        break;
      }
      std::cout << std::setw(2) << std::setfill('0') << (int)d << " ";  // 005
      index += 1;
      if (index % 16 == 0)
        printf(" | \n");
      else if (index % 8 == 0)
        printf(" | ");
      else if (index % 4 == 0)
        printf(" ");
    }
    std::cout << "\n";
    std::cout << std::dec;
  }



  static std::vector<uint8_t> SASLprep(uint8_t* src) {
    std::vector<uint8_t> rst;
    if (src) {
      uint8_t* strin = src;
      for (;;) {
        uint8_t c = *strin;
        if (!c) {
          rst.push_back(0);
          break;
        }
        switch (c) {
          case 0xAD:
            ++strin;
            break;
          case 0xA0:
          case 0x20:
            rst.push_back(0x20);
            ++strin;
            break;
          case 0x7F:
            rst.clear();
            break;
          default:
            if (c < 0x1F) {
              rst.clear();
            } else if (c >= 0x80 && c <= 0x9F) {
              rst.clear();
            } else {
              rst.push_back(c);
              ++strin;
            }
        };
      }
    }

    return rst;
  }

  static int SASLprep0(uint8_t* src, uint8_t* out) {
    if (src) {
      uint8_t* strin = src;
      uint8_t* strout = out;
      for (;;) {
        uint8_t c = *strin;
        if (!c) {
          *strout = 0;
          break;
        }
        switch (c) {
          case 0xAD:
            ++strin;
            break;
          case 0xA0:
          case 0x20:
            *strout = 0x20;
            ++strout;
            ++strin;
            break;
          case 0x7F:
            return -1;
          default:
            if (c < 0x1F) return -1;
            if (c >= 0x80 && c <= 0x9F) return -1;
            *strout = c;
            ++strout;
            ++strin;
        };
      }
    }

    return 0;
  }
};


// 2022年2月12日
namespace secure {

template <typename T>
class RandomGenerator {
 public:
  RandomGenerator(T min, T max, int seed_)
      : seed(seed_), rd{}, gen{seed == INT_MIN ? rd() : seed} {}

  virtual T next() = 0;

 protected:
  int seed;
  std::random_device rd;
  std::mt19937 gen;
};



class IntRandomGenerator : public RandomGenerator<int> {
 public:
  IntRandomGenerator(int min, int max, int seed_ = INT_MIN)
      : RandomGenerator(min, max, seed_), dis{min, max - 1} {}
  int next() override { return dis(gen); }

 private:
  std::uniform_int_distribution<> dis;
};

class DoubleRandomGenerator : public RandomGenerator<double> {
 public:
  DoubleRandomGenerator(double min, double max, int seed_ = INT_MIN)
      : RandomGenerator<double>(min, max, seed_), dis{min, max} {}
  double next() override { return dis(gen); }

 private:
  std::uniform_real_distribution<> dis;
};


// inline void uniformIntDistribution(int min, int max, int seed = INT_MIN) {
//  std::random_device rd;
//  std::mt19937 gen{seed == INT_MIN ? rd() : seed};
//  std::uniform_int_distribution<> dis(min, max - 1);
//  auto func = [=] { return dis(gen); };
//  // return func;
//};

// inline auto uniformDoubleDistribution(double min, double max, int seed = INT_MIN) {
//  std::random_device rd;
//  std::mt19937 gen{seed == INT_MIN ? rd() : seed};
//  std::uniform_real_distribution<> dis(min, max);
//  auto func = [=] { return dis(gen); };
//  return func;
//};

inline auto getCharMap() {
  static char charMap[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                           'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                           'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                           'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                           '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
  return charMap;
}

inline std::string randomChars(size_t len) {
  static auto dic = getCharMap();
  static IntRandomGenerator randomGen{0, 10 + 26 + 26, (int)time::currentTime() % INT_MAX};

  std::string out{};
  out.reserve(len);  // 2022年8月27日
  for (size_t i = 0; i < len; i++) {
    auto r = randomGen.next();
    out += dic[r];
  }
  return out;
}



inline std::vector<uint8_t> SASLprep(uint8_t* src) {
  std::vector<uint8_t> rst;
  if (src) {
    uint8_t* strin = src;
    for (;;) {
      uint8_t c = *strin;
      if (!c) {
        rst.push_back(0);
        break;
      }
      switch (c) {
        case 0xAD:
          ++strin;
          break;
        case 0xA0:
        case 0x20:
          rst.push_back(0x20);
          ++strin;
          break;
        case 0x7F:
          rst.clear();
          break;
        default:
          if (c < 0x1F) {
            rst.clear();
          } else if (c >= 0x80 && c <= 0x9F) {
            rst.clear();
          } else {
            rst.push_back(c);
            ++strin;
          }
      };
    }
  }

  return rst;
}

/*
https://github.com/coturn/coturn/blob/4ee8f8e7d8632760e0b4ed8da328974fc5d1e190/src/client/ns_turn_msg.c#L1767
*/
inline int SASLprep(uint8_t* src, uint8_t* out) {
  if (src) {
    uint8_t* strin = src;
    uint8_t* strout = out;
    for (;;) {
      uint8_t c = *strin;
      if (!c) {
        *strout = 0;
        break;
      }
      switch (c) {
        case 0xAD:
          ++strin;
          break;
        case 0xA0:
        case 0x20:
          *strout = 0x20;
          ++strout;
          ++strin;
          break;
        case 0x7F:
          return -1;
        default:
          if (c < 0x1F) return -1;
          if (c >= 0x80 && c <= 0x9F) return -1;
          *strout = c;
          ++strout;
          ++strin;
      };
    }
  }

  return 0;
}


}  // namespace secure


}  // namespace seeker