#ifndef WSW_STDTYPES_H
#define WSW_STDTYPES_H

#include "wswstringview.h"

#include <map>
#include <set>
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wsw {

using String = std::string;
using StringStream = std::stringstream;

template <typename T>
using Vector = std::vector<T>;

template <typename K, typename V>
using TreeMap = std::map<K, V>;

template <typename K, typename V>
using HashMap = std::unordered_map<K, V>;

template <typename T>
using TreeSet = std::set<T>;

template <typename T>
using HashSet = std::unordered_set<T>;

}

#endif