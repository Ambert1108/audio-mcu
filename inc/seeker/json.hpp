/**
@project seeker
@author Tao Zhang
@since 2022/2/10
@version 0.0.1 2022/2/10
*/


#pragma once

#include "nlohmann/json.hpp"

#include <iostream>


namespace seeker {

  /*
  * 一个简单json工具
  * 该工具对 nlohmann::json 进行了一步简单的封装
  * 使得可以实现类和json字符串的直接转换，而不必操作nlohmann::json类
  * 其中MyClass需要按照 nlohmann/json 库的要求进行定义，具体参考：https://github.com/nlohmann/json/blob/develop/README.md#arbitrary-types-conversions
  */
  namespace json {
    template<typename MyClass>
    inline std::string toJsonString(const MyClass& myclass, int indent = -1) {
      nlohmann::json j = myclass;
      auto jsonString = j.dump(indent);
      return jsonString;
    }

    template<typename MyClass>
    inline MyClass fromJsonString(std::string jsonString) {
      nlohmann::json j = nlohmann::json::parse(jsonString);
      MyClass rst = j.get<MyClass>();
      return rst;
    }


    template<typename MyClass>
    inline MyClass& fromJsonString(MyClass& myclass, std::string jsonString) {
      nlohmann::json j = nlohmann::json::parse(jsonString);
      j.get_to(myclass);
      return myclass;
    }

  }
}










