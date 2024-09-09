/**
@project seeker
@author Tao Zhang
@since 2022/4/11
@version 0.1.0-SNAPSHOT 2022/9/7

@description: 一个超级简单的 jtilly/inih 封装
https://github.com/jtilly/inih
*/



#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif

#include "utils/INIReader.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <iostream>

namespace seeker {

class IniConfig {
 private:
  const INIReader iniReader;
  IniConfig(const std::string& iniFilePath) : iniReader(iniFilePath) {
    if (iniReader.ParseError() < 0) {
      throw std::runtime_error("Can't load ini file: [" + iniFilePath + "]");
    }
  }


 public:
  IniConfig(const IniConfig&) = delete;
  IniConfig& operator=(const IniConfig&) = delete;

  inline static IniConfig* instence = nullptr;

  static void init(const std::string& iniFilePath) {
    if (instence != nullptr) {
      delete instence;
      instence = nullptr;
    }
    instence = new IniConfig(iniFilePath);
  }

  static const INIReader& getReader() {
    if (instence == nullptr) {
      throw std::runtime_error("Init IniConfig first!");
    }
    return instence->iniReader;
  }


  static std::string Get(const std::string& section, const std::string& name,
                         const std::string& default_value) {
    return instence->iniReader.Get(section, name, default_value);
  }


  static long GetInteger(const std::string& section, const std::string& name,
                         long default_value) {
    return instence->iniReader.GetInteger(section, name, default_value);
  }



  static double GetReal(const std::string& section, const std::string& name,
                        double default_value) {
    return instence->iniReader.GetReal(section, name, default_value);
  }


  static float GetFloat(const std::string& section, const std::string& name,
                        float default_value) {
    return instence->iniReader.GetFloat(section, name, default_value);
  }


  static bool GetBoolean(const std::string& section, const std::string& name,
                         bool default_value) {
    return instence->iniReader.GetBoolean(section, name, default_value);
  }
};



}  // namespace seeker
